#!/usr/bin/env python3
"""Convert a recovery classifier checkpoint (timm resnet18, 13 input channels, one logit) into the
plugin's weight file. Batch normalisation is folded into each convolution; weights are stored as
float16, biases as float32.

  python3 tools/convert_recovery_model.py checkpoint.pt ofx/resources/recovery-model.bin

File layout (little-endian): magic "ADRRM001", uint32 layer count, then per layer:
uint32 kind (0 conv, 1 fc), uint32 cout, cin, k, stride, pad, flags (bit0 relu, bit1 adds the
residual saved before the block, bit2 saves this layer's input as residual, bit3 is the block's
downsample: its output becomes the residual), float16 weights [cout][cin][k][k], float32 bias [cout].
"""
import struct, sys
import numpy as np
import torch

def fold(sd, conv, bn):
    w = sd[f'{conv}.weight'].double()
    g, b, m, v = (sd[f'{bn}.{n}'].double() for n in ('weight', 'bias', 'running_mean', 'running_var'))
    s = g / torch.sqrt(v + 1e-5)
    return (w * s[:, None, None, None]).float().numpy(), (b - m * s).float().numpy()

def layers(sd):
    out = []
    w, b = fold(sd, 'conv1', 'bn1'); out.append((0, w, b, 2, 3, 1))           # conv 7x7 s2 + relu (then maxpool)
    for li, ch in enumerate((64, 128, 256, 512), 1):
        for blk in range(2):
            p = f'layer{li}.{blk}'
            stride = 2 if li > 1 and blk == 0 else 1
            has_ds = f'{p}.downsample.0.weight' in sd
            w, b = fold(sd, f'{p}.conv1', f'{p}.bn1'); out.append((0, w, b, stride, 1, 1 | 4))
            if has_ds:
                w2, b2 = fold(sd, f'{p}.downsample.0', f'{p}.downsample.1'); out.append((0, w2, b2, stride, 0, 8))
            w, b = fold(sd, f'{p}.conv2', f'{p}.bn2'); out.append((0, w, b, 1, 1, 1 | 2))
    fw = sd['fc.weight'].float().numpy().reshape(1, 512, 1, 1); fb = sd['fc.bias'].float().numpy()
    out.append((1, fw, fb, 1, 0, 0))
    return out

def main():
    sd = torch.load(sys.argv[1], map_location='cpu')
    sd = sd.get('state_dict', sd) if isinstance(sd, dict) else sd.state_dict()
    L = layers(sd)
    with open(sys.argv[2], 'wb') as f:
        f.write(b'ADRRM001'); f.write(struct.pack('<I', len(L)))
        for kind, w, b, stride, pad, flags in L:
            co, ci, k, _ = w.shape
            f.write(struct.pack('<7I', kind, co, ci, k, stride, pad, flags))
            f.write(np.ascontiguousarray(w, '<f2').tobytes()); f.write(np.ascontiguousarray(b, '<f4').tobytes())
    print(f'{len(L)} layers written to {sys.argv[2]}')

if __name__ == '__main__':
    main()
