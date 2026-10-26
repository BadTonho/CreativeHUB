# RNNoise standard model v1

`std.rnnn` contains the original RNNoise v0.1 quantized weights, converted to
the rnnoise-nu text format accepted by FFmpeg's `arnndn`. It is not the newer
RNNoise binary weights format.

- Converted artifact: https://github.com/richardpl/arnndn-models/blob/c670d8220c9db2667f0879541f9dd80bc5abfbdc/std.rnnn
- Original weights: https://github.com/xiph/rnnoise/blob/v0.1/src/rnn_data.c
- Conversion format: dense layer dimensions/activation, input weights/bias;
  GRU layer dimensions/activation, input/recurrent weights/bias. Layer order is
  input dense, VAD GRU, noise GRU, denoise GRU, denoise output, VAD output.
- Artifact SHA-256: `6b8943dc4a9b6b24425873992a44f29c0577503276456af46a8854774faeb294`
- Size: 302,903 bytes. Original RNNoise BSD-3-Clause terms are in `COPYING`.

The application embeds this fixed model, extracts it atomically on first use,
and verifies its hash. It never downloads a model at runtime. Source code,
weights and copyright notices must remain available together in distributions.

The conversion was checked against the RNNoise v0.1 C arrays: all 87,503
quantized weights/biases match across the six layers.
