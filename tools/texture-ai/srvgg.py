"""A pure-numpy inference for the Real-ESRGAN "compact" super-resolution networks (SRVGGNetCompact: realesr-animevideov3, realesr-general-x4v3).

No PyTorch is needed. The weights file is read with a restricted unpickler that can only build tensors and ordered dictionaries, so a downloaded
model file cannot run code. The network is a stack of 3x3 convolutions with PReLU activations, a last convolution to 3 * scale^2 channels, a pixel
shuffle, and the nearest-neighbour upscale of the input added back (a residual). Textures tile, so the input is padded by wrapping round its edges
and the result cropped: the upscaled texture still tiles seamlessly.

Model licence: Real-ESRGAN is BSD-3-Clause (https://github.com/xinntao/Real-ESRGAN), weights from its v0.2.5.0 release. See THIRD_PARTY_NOTICES.md.
"""
import collections
import io
import pickle
import zipfile

import numpy as np


class _Storage:
    def __init__(self, dtype):
        self.dtype = dtype


def load_weights(path):
    """Reads a PyTorch .pth (a zip of a pickle and raw tensors) into {name: float32 array}, without PyTorch and without executing anything."""
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        pkl = next(n for n in names if n.endswith("data.pkl"))
        prefix = pkl[: -len("data.pkl")]

        def rebuild(storage, offset, size, stride, *rest):
            key, dtype = storage
            raw = np.frombuffer(z.read(f"{prefix}data/{key}"), dtype=dtype)
            if len(size) == 0:
                return raw[offset:offset + 1].reshape(()).astype(np.float32)
            item = raw.dtype.itemsize
            view = np.lib.stride_tricks.as_strided(raw[offset:], shape=tuple(size), strides=tuple(s * item for s in stride))
            return np.array(view, dtype=np.float32)

        class Restricted(pickle.Unpickler):
            def find_class(self, module, name):
                if (module, name) == ("collections", "OrderedDict"):
                    return collections.OrderedDict
                if module == "torch._utils" and name == "_rebuild_tensor_v2":
                    return rebuild
                if module == "torch" and name in ("FloatStorage", "DoubleStorage", "HalfStorage"):
                    return _Storage(name)
                raise pickle.UnpicklingError(f"refusing to load {module}.{name}")

            def persistent_load(self, pid):
                tag, storage_type, key, _location, _numel = pid
                dtype = {"FloatStorage": np.float32, "DoubleStorage": np.float64, "HalfStorage": np.float16}[storage_type.dtype]
                return (key, dtype)

        obj = Restricted(io.BytesIO(z.read(pkl))).load()
    if isinstance(obj, dict) and "params" in obj:
        obj = obj["params"]
    elif isinstance(obj, dict) and "params_ema" in obj:
        obj = obj["params_ema"]
    return {k: np.asarray(v, dtype=np.float32) for k, v in obj.items()}


def conv3x3(x, w, b):
    """x: (H, W, Cin) float32, w: (Cout, Cin, 3, 3), b: (Cout,). Zero padding (the caller pads by wrapping)."""
    h, wd, cin = x.shape
    cout = w.shape[0]
    p = np.pad(x, ((1, 1), (1, 1), (0, 0)))
    cols = np.empty((h * wd, 9 * cin), dtype=np.float32)
    k = 0
    for dy in range(3):
        for dx in range(3):
            cols[:, k * cin:(k + 1) * cin] = p[dy:dy + h, dx:dx + wd, :].reshape(h * wd, cin)
            k += 1
    wm = w.transpose(2, 3, 1, 0).reshape(9 * cin, cout)   # (dy, dx, cin) order, matching the columns
    return (cols @ wm + b).reshape(h, wd, cout)


def prelu(x, a):
    return np.where(x >= 0, x, x * a)


def upscale(weights, image, scale=4, wrap=True):
    """image: float32 (H, W, 3) in 0..1. Returns float32 (H*scale, W*scale, 3) in 0..1."""
    conv_ids = sorted({int(k.split(".")[1]) for k in weights if k.startswith("body.") and k.endswith(".weight") and weights[k].ndim == 4})
    pad = len(conv_ids) + 2   # the receptive field's radius, so the cropped result is exact
    h, w, _ = image.shape
    x = np.pad(image, ((pad, pad), (pad, pad), (0, 0)), mode="wrap" if wrap else "edge")
    base = x
    for n, i in enumerate(conv_ids):
        x = conv3x3(x, weights[f"body.{i}.weight"], weights[f"body.{i}.bias"])
        if n < len(conv_ids) - 1:
            x = prelu(x, weights[f"body.{i + 1}.weight"].reshape(1, 1, -1))
    hh, ww, c = x.shape
    out = x.reshape(hh, ww, 3, scale, scale).transpose(0, 3, 1, 4, 2).reshape(hh * scale, ww * scale, 3)   # pixel shuffle
    out = out + np.repeat(np.repeat(base, scale, axis=0), scale, axis=1)
    out = out[pad * scale:(pad + h) * scale, pad * scale:(pad + w) * scale]
    return np.clip(out, 0.0, 1.0)
