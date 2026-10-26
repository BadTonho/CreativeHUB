# Speech regression fixture

`speech.flac` is the unmodified LibriSpeech test-clean utterance
`6930/75918/6930-75918-0000.flac`, from
https://www.openslr.org/resources/12/test-clean.tar.gz.

LibriSpeech was prepared by Vassil Panayotov, Guoguo Chen, Daniel Povey and
Sanjeev Khudanpur from LibriVox recordings. It is distributed under CC BY 4.0:
https://creativecommons.org/licenses/by/4.0/ . Resource and attribution:
https://www.openslr.org/12/ . No endorsement is implied.

SHA-256: `9ce35224156f071ab58eb7feb8a5ceae600f6f9f353da2a6cbf797b6b1ac8a23`.
Size: 74,138 bytes. Tests decode a short section locally and add deterministic
noise; no network or speech synthesis runtime is used by tests or the app.
