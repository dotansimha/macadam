/* Copyright 2017 Streampunk Media Ltd.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/* -LICENSE-START-
 ** Copyright (c) 2010 Blackmagic Design
 **
 ** Permission is hereby granted, free of charge, to any person or organization
 ** obtaining a copy of the software and accompanying documentation covered by
 ** this license (the "Software") to use, reproduce, display, distribute,
 ** execute, and transmit the Software, and to prepare derivative works of the
 ** Software, and to permit third-parties to whom the Software is furnished to
 ** do so, all subject to the following:
 **
 ** The copyright notices in the Software and this entire statement, including
 ** the above license grant, this restriction and the following disclaimer,
 ** must be included in all copies of the Software, in whole or in part, and
 ** all derivative works of the Software, unless such copies or derivative
 ** works are solely in the form of machine-executable object code generated by
 ** a source language processor.
 **
 ** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 ** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 ** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 ** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 ** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 ** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 ** DEALINGS IN THE SOFTWARE.
 ** -LICENSE-END-
 */

#include "Playback.h"
#include <string.h>

namespace streampunk {

inline Nan::Persistent<v8::Function> &Playback::constructor() {
  static Nan::Persistent<v8::Function> myConstructor;
  return myConstructor;
}

Playback::Playback(uint32_t deviceIndex, uint32_t displayMode,
    uint32_t pixelFormat) : m_totalFrameScheduled(0), deviceIndex_(deviceIndex),
    displayMode_(displayMode), pixelFormat_(pixelFormat), result_(0) {
  async = new uv_async_t;
  uv_async_init(uv_default_loop(), async, FrameCallback);
  uv_mutex_init(&padlock);
  async->data = this;
}

Playback::~Playback() {
  if (!playbackCB_.IsEmpty())
    playbackCB_.Reset();
}

NAN_MODULE_INIT(Playback::Init) {
  #ifdef WIN32
  HRESULT result;
  result = CoInitialize(NULL);
	if (FAILED(result))
	{
		fprintf(stderr, "Initialization of COM failed - result = %08x.\n", result);
	}
  #endif

  // Prepare constructor template
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Playback").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  // Prototype
  Nan::SetPrototypeMethod(tpl, "init", BMInit);
  Nan::SetPrototypeMethod(tpl, "scheduleFrame", ScheduleFrame);
  Nan::SetPrototypeMethod(tpl, "doPlayback", DoPlayback);
  Nan::SetPrototypeMethod(tpl, "stop", StopPlayback);
  Nan::SetPrototypeMethod(tpl, "enableAudio", EnableAudio);
  Nan::SetPrototypeMethod(tpl, "testStuff", TestStuff);

  constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("Playback").ToLocalChecked(),
               Nan::GetFunction(tpl).ToLocalChecked());
}

NAN_METHOD(Playback::New) {
  if (info.IsConstructCall()) {
    // Invoked as constructor: `new Playback(...)`
    uint32_t deviceIndex = info[0]->IsUndefined() ? 0 : Nan::To<uint32_t>(info[0]).FromJust();
    uint32_t displayMode = info[1]->IsUndefined() ? 0 : Nan::To<uint32_t>(info[1]).FromJust();
    uint32_t pixelFormat = info[2]->IsUndefined() ? 0 : Nan::To<uint32_t>(info[2]).FromJust();
    Playback* obj = new Playback(deviceIndex, displayMode, pixelFormat);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    // Invoked as plain function `Playback(...)`, turn into construct call.
    const int argc = 3;
    v8::Local<v8::Value> argv[argc] = { info[0], info[1], info[2] };
    v8::Local<v8::Function> cons = Nan::New(constructor());
    info.GetReturnValue().Set(Nan::NewInstance(cons, argc, argv).ToLocalChecked());
  }
}

NAN_METHOD(Playback::BMInit) {
  IDeckLinkIterator* deckLinkIterator;
  HRESULT	result;
  IDeckLinkAPIInformation *deckLinkAPIInformation;
  IDeckLink* deckLink;
  #ifdef WIN32
  CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator);
  #else
  deckLinkIterator = CreateDeckLinkIteratorInstance();
  #endif
  result = deckLinkIterator->QueryInterface(IID_IDeckLinkAPIInformation, (void**)&deckLinkAPIInformation);
  if (result != S_OK) {
    Nan::ThrowError("Error connecting to DeckLinkAPI.\n");
  }
  Playback* obj = ObjectWrap::Unwrap<Playback>(info.Holder());

  for ( uint32_t x = 0 ; x <= obj->deviceIndex_ ; x++ ) {
    if (deckLinkIterator->Next(&deckLink) != S_OK) {
      info.GetReturnValue().SetUndefined();
      return;
    }
  }

  obj->m_deckLink = deckLink;

  IDeckLinkOutput *deckLinkOutput;
  if (deckLink->QueryInterface(IID_IDeckLinkOutput, (void **)&deckLinkOutput) != S_OK)
  {
    Nan::ThrowError("Could not obtain DeckLink Output interface.\n");
  }
  obj->m_deckLinkOutput = deckLinkOutput;

  if (obj->setupDeckLinkOutput())
    info.GetReturnValue().Set(Nan::New("made it!").ToLocalChecked());
  else
    info.GetReturnValue().Set(Nan::New("sad :-(").ToLocalChecked());
}

NAN_METHOD(Playback::DoPlayback) {
  v8::Local<v8::Function> cb = v8::Local<v8::Function>::Cast(info[0]);
  Playback* obj = ObjectWrap::Unwrap<Playback>(info.Holder());
  obj->playbackCB_.Reset(cb);

  if (obj->hasAudio_) {
    if (obj->m_deckLinkOutput->EndAudioPreroll() != S_OK)
      printf("Failed to end audio preroll.\n");
  }

  int result = obj->m_deckLinkOutput->StartScheduledPlayback(0, obj->m_timeScale, 1.0);
  // printf("Playback result code %i and timescale %I64d.\n", result, obj->m_timeScale);

  if (result == S_OK) {
    info.GetReturnValue().Set(Nan::New("Playback started.").ToLocalChecked());
  }
  else {
    info.GetReturnValue().Set(Nan::New("Playback failed to start.").ToLocalChecked());
  }
}

NAN_METHOD(Playback::StopPlayback) {
  Playback* obj = ObjectWrap::Unwrap<Playback>(info.Holder());

  obj->cleanupDeckLinkOutput();

  obj->playbackCB_.Reset();

  info.GetReturnValue().Set(Nan::New("Playback stopped.").ToLocalChecked());
}

NAN_METHOD(Playback::TestStuff) {
  printf("Test stuff %i.\n", info.Length());
  Nan::MaybeLocal<v8::Object> audObj = Nan::MaybeLocal<v8::Object>();
  printf("What is its value? %i\n", audObj.IsEmpty());
}

NAN_METHOD(Playback::ScheduleFrame) {
  Playback* obj = ObjectWrap::Unwrap<Playback>(info.Holder());
  v8::Local<v8::Object> bufObj = Nan::To<v8::Object>(info[0]).ToLocalChecked();
  Nan::MaybeLocal<v8::Object> audBufObj = Nan::MaybeLocal<v8::Object>();
  if (info.Length() >= 2) audBufObj = Nan::To<v8::Object>(info[1]);
  bool processAudio = obj->hasAudio_ && !audBufObj.IsEmpty();

  uint32_t rowBytePixelRatioN = 1, rowBytePixelRatioD = 1;
  switch (obj->pixelFormat_) { // TODO expand to other pixel formats
    case bmdFormat10BitYUV:
      rowBytePixelRatioN = 8; rowBytePixelRatioD = 3;
      break;
    default:
      rowBytePixelRatioN = 2; rowBytePixelRatioD = 1;
      break;
  }

  IDeckLinkMutableVideoFrame* frame;
  if (obj->m_deckLinkOutput->CreateVideoFrame(obj->m_width, obj->m_height,
      obj->m_width * rowBytePixelRatioN / rowBytePixelRatioD,
      (BMDPixelFormat) obj->pixelFormat_, bmdFrameFlagDefault, &frame) != S_OK) {
    info.GetReturnValue().Set(Nan::New("Failed to create frame.").ToLocalChecked());
    return;
  };
  char* bufData = node::Buffer::Data(bufObj);
  size_t bufLength = node::Buffer::Length(bufObj);
  char* frameData = NULL;
  if (frame->GetBytes((void**) &frameData) != S_OK) {
    info.GetReturnValue().Set(Nan::New("Failed to get new frame bytes.").ToLocalChecked());
    return;
  };
  memcpy(frameData, bufData, bufLength);

  // printf("Frame duration %I64d/%I64d.\n", obj->m_frameDuration, obj->m_timeScale);
  uv_mutex_lock(&obj->padlock);
  HRESULT sfr = obj->m_deckLinkOutput->ScheduleVideoFrame(frame,
      (obj->m_totalFrameScheduled * obj->m_frameDuration),
      obj->m_frameDuration, obj->m_timeScale);
  if (sfr != S_OK) {
    printf("Failed to schedule frame. Code is %i.\n", sfr);
    info.GetReturnValue().Set(Nan::New("Failed to schedule frame.").ToLocalChecked());
    uv_mutex_unlock(&obj->padlock);
    return;
  };

  if (processAudio) {
    uint32_t sampleFramesWritten = NULL;
    HRESULT saud = obj->m_deckLinkOutput->ScheduleAudioSamples(
      node::Buffer::Data(audBufObj.ToLocalChecked()),
      node::Buffer::Length(audBufObj.ToLocalChecked()) / obj->sampleByteFactor_,
      obj->m_totalSampleScheduled,
      obj->audioSampleRate_, &sampleFramesWritten);
    obj->m_totalSampleScheduled += sampleFramesWritten;
    if (saud != S_OK) {
      printf("Failed to schedule audio. Code is %i.\n", saud);
      info.GetReturnValue().Set(Nan::New("Failed to schedule audio.").ToLocalChecked());
      uv_mutex_unlock(&obj->padlock);
      return;
    }
  }

  obj->m_totalFrameScheduled++;
  uv_mutex_unlock(&obj->padlock);
  info.GetReturnValue().Set(obj->m_totalFrameScheduled);
}

NAN_METHOD(Playback::EnableAudio) {
  Playback* obj = ObjectWrap::Unwrap<Playback>(info.Holder());
  HRESULT result;
  BMDAudioSampleRate sampleRate = info[0]->IsNumber() ?
      (BMDAudioSampleRate) Nan::To<uint32_t>(info[0]).FromJust() : bmdAudioSampleRate48kHz;
  BMDAudioSampleType sampleType = info[1]->IsNumber() ?
      (BMDAudioSampleType) Nan::To<uint32_t>(info[1]).FromJust() : bmdAudioSampleType16bitInteger;
  uint32_t channelCount = info[2]->IsNumber() ? Nan::To<uint32_t>(info[2]).FromJust() : 2;

  // Setting stream type as timestamped - should be good enough
  result = obj->setupAudioOutput(sampleRate, sampleType, channelCount, bmdAudioOutputStreamTimestamped);

  switch (result) {
    case E_INVALIDARG:
      info.GetReturnValue().Set(
        Nan::New<v8::String>("audio channel count must be 2, 8 or 16").ToLocalChecked());
      break;
    case E_ACCESSDENIED:
      info.GetReturnValue().Set(
        Nan::New<v8::String>("unable to access the hardware or audio output not enabled.").ToLocalChecked());
      break;
    case S_OK:
      info.GetReturnValue().Set(Nan::New<v8::String>("audio enabled").ToLocalChecked());
      break;
    default:
      info.GetReturnValue().Set(Nan::New<v8::String>("failed to start audio").ToLocalChecked());
      break;
  }
}

bool Playback::setupDeckLinkOutput() {
  // bool							result = false;
  IDeckLinkDisplayModeIterator*	displayModeIterator = NULL;
  IDeckLinkDisplayMode*			deckLinkDisplayMode = NULL;

  m_width = -1;

  // set callback
  m_deckLinkOutput->SetScheduledFrameCompletionCallback(this);

  // get frame scale and duration for the video mode
  if (m_deckLinkOutput->GetDisplayModeIterator(&displayModeIterator) != S_OK)
    return false;

  while (displayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
  {
    if (deckLinkDisplayMode->GetDisplayMode() == displayMode_)
    {
      m_width = deckLinkDisplayMode->GetWidth();
      m_height = deckLinkDisplayMode->GetHeight();
      deckLinkDisplayMode->GetFrameRate(&m_frameDuration, &m_timeScale);
      deckLinkDisplayMode->Release();

      break;
    }

    deckLinkDisplayMode->Release();
  }

  displayModeIterator->Release();

  if (m_width == -1)
    return false;

  if (m_deckLinkOutput->EnableVideoOutput((BMDDisplayMode) displayMode_, bmdVideoOutputFlagDefault) != S_OK)
    return false;

  return true;
}

HRESULT	Playback::ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
  uv_mutex_lock(&padlock);
  result_ = result;
  completedFrame->Release(); // Assume you should do this
  uv_mutex_unlock(&padlock);
  uv_async_send(async);
	return S_OK;
}

void Playback::cleanupDeckLinkOutput()
{
	m_deckLinkOutput->StopScheduledPlayback(0, NULL, 0);
	m_deckLinkOutput->DisableVideoOutput();
	m_deckLinkOutput->SetScheduledFrameCompletionCallback(NULL);
}

HRESULT Playback::setupAudioOutput(BMDAudioSampleRate sampleRate, BMDAudioSampleType sampleType,
  uint32_t channelCount, BMDAudioOutputStreamType streamType) {

  hasAudio_ = true;
  audioSampleRate_ = sampleRate;
  sampleByteFactor_ = channelCount * (sampleType / 8);
  m_totalSampleScheduled = 0;
  HRESULT result = m_deckLinkOutput->EnableAudioOutput(sampleRate, sampleType, channelCount, streamType);

  if (m_deckLinkOutput->BeginAudioPreroll() != S_OK)
    printf("Failed to begin audio preroll.\n");

  return result;
}

NAUV_WORK_CB(Playback::FrameCallback) {
  Nan::HandleScope scope;
  Playback *playback = static_cast<Playback*>(async->data);
  uv_mutex_lock(&playback->padlock);
  if (!playback->playbackCB_.IsEmpty()) {
    Nan::Callback cb(Nan::New(playback->playbackCB_));

    v8::Local<v8::Value> argv[1] = { Nan::New(playback->result_) };
    cb.Call(1, argv);
  } else {
    printf("Frame callback is empty. Assuming finished.\n");
  }
  uv_mutex_unlock(&playback->padlock);
}

}
