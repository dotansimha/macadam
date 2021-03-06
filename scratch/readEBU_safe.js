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

var H = require('highland');
var fs = require('fs');
var mac = require('../index.js');

var readdir = H.wrapCallback(fs.readdir);
var readFile = H.wrapCallback(fs.readFile);

var playback = new mac.Playback(0, mac.bmdModeHD1080i50, mac.bmdFormat10BitYUV);

var baseFolder = "E:/media/EBU_test_sets/filexchange.ebu.ch/EBU test sets - Creative Commons (BY-NC-ND)/HDTV test sequences/1080i25/rainroses_1080i_/rainroses_1080i_"
var count = 0;

H((push, next) => { push(null, baseFolder); next(); })
  .take(1)
  .flatmap(x => readdir(x).filter(y => y.endsWith('yuv10')).flatten().sort())
  .map(readFile)
  .parallel(4)
  .ratelimit(1, 40)
  .doto(x => { playback.frame(x); })
  .doto(() => if (count++ == 4) { playback.start(); })
  .error(H.log)
  .done(() => { playback.stop(); })

process.on('SIGINT', () => {
  playback.stop();
  process.exit();
});                                                                                      
