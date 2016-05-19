/* Copyright 2016 Streampunk Media Ltd

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

#ifndef PLAYBACK_H
#define PLAYBACK_H

#include <node.h>
#include <node_object_wrap.h>
#include <uv.h>
#include <node_buffer.h>

#include "DeckLinkAPI.h"

namespace streampunk {

class Playback : public IDeckLinkVideoOutputCallback, public node::ObjectWrap
{
private:
  explicit Playback(uint32_t deviceIndex = 0, uint32_t displayMode = 0, uint32_t pixelFormat = 0);
  ~Playback();

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static v8::Persistent<v8::Function> constructor;

	IDeckLink *					m_deckLink;
	IDeckLinkOutput *			m_deckLinkOutput;
  uv_async_t *async;

	// The mutex and condition variable are used to wait for
	// - a deck to be connected
	// - the export to complete
	// pthread_mutex_t				m_mutex ;
	// pthread_cond_t				m_condition;
	bool						m_waitingForDeckConnected;
	bool						m_waitingForExportEnd;
	bool						m_exportStarted;

	// array of coloured frames
	IDeckLinkMutableVideoFrame** m_videoFrames;
	uint32_t					m_nextFrameIndex;
	uint32_t					m_totalFrameScheduled;

	// video mode
	long						m_width;
	long						m_height;
	BMDTimeScale				m_timeScale;
	BMDTimeValue				m_frameDuration;

	bool			fillFrame(int index);
	void			releaseFrames();
	bool			createFrames();

	bool			setupDeckLinkOutput();

	bool			scheduleNextFrame(bool preroll);

	void			cleanupDeckLinkOutput();

  static void BMInit(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void DoPlayback(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void StopPlayback(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void ScheduleFrame(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void FrameCallback(uv_async_t *handle);

  uint32_t deviceIndex_;
  uint32_t displayMode_;
  uint32_t pixelFormat_;
  v8::Persistent<v8::Function> playbackCB_;
  uint32_t result_;
public:
  static void Init(v8::Local<v8::Object> exports);

	// IDeckLinkVideoOutputCallback
	virtual HRESULT	ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
	virtual HRESULT	ScheduledPlaybackHasStopped () {return S_OK;};

	// IUnknown
	HRESULT			QueryInterface (REFIID iid, LPVOID *ppv)	{return E_NOINTERFACE;}
	ULONG			AddRef ()									{return 1;}
	ULONG			Release ()									{return 1;}
};

}

#endif
