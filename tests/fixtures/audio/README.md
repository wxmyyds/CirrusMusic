# Audio fixtures

Synthetic tones only (no recordings): 0.5 s, 44.1 kHz stereo, 440 Hz left and 660 Hz right,
22050 frames. Used by `tst_audio_decoder` and `tst_audio_engine`.

`long.mp3`: 4 s, 22.05 kHz mono, 330 Hz, long enough for a transition (`tst_audio_engine`).

`seek.mp3`: 200 s, 8 kHz mono, 330 Hz at 8 kbit/s (200 KB): longer than what opening reads,
so a test can seek past what has downloaded (`tst_audio_engine`).

`fade.mp3`: 8.5 s, 22.05 kHz mono, 550 Hz: 0.5 s of silence, 4 s of tone, a fade-out of
17 dB a second for 3 s, silence (a natural end, `tst_audio_engine`).

`tone_aac.m4a`, `tone_fmp4.m4a`: the same 0.5 s tone as MP4 audio, for the
decoder's MP4 container paths (`tst_audio_decoder`). `tone_aac.m4a` has its `moov` after the
`mdat`; `tone_fmp4.m4a` is fragmented (moof/mdat, no trailing moov). The codec is named by
`format`, never by the file suffix.

`tone_eac3.m4a` is the same tone encoded as E-AC-3. It is a negative fixture: the
application consistently rejects Dolby audio, independent of the platform FFmpeg build.

`tone_flac.m4a` wraps the existing `tone.flac` frames without encoding in an ISO BMFF
container: `ftyp(isom/iso2/mp41)`, one `mdat`, then `moov` with a single `soun` track,
`fLaC` sample entry and `dfLa` STREAMINFO. The six frame sizes are 2491, 2488, 2494, 2488,
2495 and 970 bytes, starting at byte 8304 in `tone.flac`; five last 4096 samples, the last
1570. The 34-byte STREAMINFO is at offset 8. This covers `format=flac-mp4` separately from
native `flac`. `ffprobe` inspection reports FLAC, 44100 Hz, stereo, 0.5 s and six frames;
actual decoder execution is verified only by GitHub Actions.

```bash
ffmpeg -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)|0.5*sin(2*PI*660*t):s=44100:d=0.5" -c:a pcm_s16le tone.wav
lame -b 128 tone.wav tone.mp3
flac -8 tone.wav -o tone.flac
ffmpeg -i tone.wav -c:a aac -b:a 128k -f mp4 tone_aac.m4a
ffmpeg -i tone.wav -c:a aac -b:a 128k -movflags frag_keyframe+empty_moov+default_base_moof -f mp4 tone_fmp4.m4a
ffmpeg -i tone.wav -c:a eac3 -b:a 192k -f mp4 tone_eac3.m4a
ffmpeg -f lavfi -i "aevalsrc=0.3*sin(2*PI*330*t):s=22050:d=4" -c:a pcm_s16le long.wav
lame -b 32 long.wav long.mp3
ffmpeg -f lavfi -i "aevalsrc='if(lt(t,0.5),0,if(lt(t,4.5),0.3*sin(2*PI*550*t),if(lt(t,7.5),0.3*pow(10,-(t-4.5)*17/20)*sin(2*PI*550*t),0)))':s=22050:d=8.5" -c:a pcm_s16le fade.wav
lame -b 32 fade.wav fade.mp3
ffmpeg -f lavfi -i "aevalsrc=0.3*sin(2*PI*330*t):s=8000:d=200" -c:a pcm_s16le seek.wav
lame -b 8 seek.wav seek.mp3
```
