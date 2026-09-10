"""ctypes access to Matcha, with an optional standard Gymnasium environment.

NativeBatch.ram()/observation() borrow actual machine memory. Views stay alive
after close (they retain storage), and change after step/reset. Copy a view when
storing it in a replay buffer. Never read a borrowed view during a concurrent
step on that same batch. The convenience observations() call explicitly copies.
"""
from __future__ import annotations
import ctypes as C
from pathlib import Path
import sys
import threading
import numpy as np

try:
    import gymnasium as gym
except ImportError:
    gym = None


def _library(path=None):
    extension = "dylib" if sys.platform == "darwin" else "so"
    lib = C.CDLL(str(Path(path) if path else Path(__file__).parent / "build" / f"libmatcha.{extension}"))
    pointer = C.POINTER(C.c_uint8)
    signatures = {
        "gym_create": (C.c_void_p, [C.c_char_p, C.c_int]),
        "gym_destroy": (None, [C.c_void_p]),
        "gym_last_error": (C.c_char_p, []),
        "gym_count": (C.c_int, [C.c_void_p]),
        "gym_step_checked": (C.c_int, [C.c_void_p, pointer, C.c_size_t, C.POINTER(C.c_float), pointer]),
        "gym_reset": (C.c_int, [C.c_void_p, C.c_int]),
        "gym_observation_ptr": (C.c_int, [C.c_void_p, C.c_int, C.POINTER(pointer), C.POINTER(C.c_size_t)]),
        "gym_get_ram": (None, [C.c_void_p, C.c_int, C.POINTER(pointer)]),
        "gym_ram_size": (C.c_int, [C.c_void_p]),
        "gym_cycles": (C.c_uint64, [C.c_void_p, C.c_int]),
        "gym_frames": (C.c_uint64, [C.c_void_p, C.c_int]),
        "gym_peek": (C.c_int, [C.c_void_p, C.c_int, C.c_uint16, pointer]),
        "gym_add_watch": (C.c_int, [C.c_void_p, C.c_uint16, C.c_uint8, C.c_float, C.c_uint8, C.c_uint8]),
        "gym_clear_watches": (C.c_int, [C.c_void_p]),
        "gym_set_terminal": (C.c_int, [C.c_void_p, C.c_uint16, C.c_uint8, C.c_uint8]),
        "gym_clear_terminal": (C.c_int, [C.c_void_p]),
        "gym_set_max_steps": (C.c_int, [C.c_void_p, C.c_uint64]),
        "gym_episode_flags": (C.c_int, [C.c_void_p, C.c_int, pointer, pointer]),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(lib, name)
        function.restype, function.argtypes = result, arguments
    return lib


class _Storage:
    def __init__(self, library, handle):
        self.library, self.handle = library, handle

    def __del__(self):
        if self.handle:
            self.library.gym_destroy(self.handle)
            self.handle = None


class NativeBatch:
    """N independent machines, with one synchronous step per 70,224-T quantum.

    CPU instructions finish at the boundary with their excess clocks carried
    forward. STOP may return without oscillator clocks, allowing an input to
    wake it. cycles() reports actual emulated clocks, not invented frames.
    """
    def __init__(self, rom_path, num_instances=1, *, library=None):
        if not isinstance(num_instances, int) or not 1 <= num_instances <= 256:
            raise ValueError("num_instances must be an integer in 1..256")
        self._lock = threading.RLock()
        self._lib = _library(library)
        handle = self._lib.gym_create(str(Path(rom_path).resolve()).encode(), num_instances)
        if not handle:
            raise RuntimeError(self._lib.gym_last_error().decode())
        self._storage = _Storage(self._lib, handle)
        self.count = num_instances
        self._rewards = np.zeros(self.count, dtype=np.float32)
        self._dones = np.zeros(self.count, dtype=np.uint8)

    def _handle(self):
        if self._storage is None:
            raise RuntimeError("batch is closed")
        return self._storage.handle

    def _check(self, status):
        if status != 0:
            raise RuntimeError(self._lib.gym_last_error().decode())

    def _index(self, index):
        if not isinstance(index, (int, np.integer)) or not 0 <= index < self.count:
            raise IndexError("invalid environment index")
        return int(index)

    def _borrow(self, pointer, length, *, readonly=True):
        buffer = (C.c_uint8 * length).from_address(C.addressof(pointer.contents))
        # NumPy's memoryview retains the ctypes buffer, which owns the native
        # storage lease even if callers strip subclasses or reshape the view.
        buffer._matcha_storage = self._storage
        result = np.ctypeslib.as_array(buffer)
        if readonly:
            result.setflags(write=False)
        return result

    def ram(self, index=0, *, writable=False):
        """Borrow real 8 KiB WRAM (C000-DFFF), not a flattened MMIO copy."""
        with self._lock:
            pointer = C.POINTER(C.c_uint8)()
            self._lib.gym_get_ram(self._handle(), self._index(index), C.byref(pointer))
            if not pointer:
                raise RuntimeError(self._lib.gym_last_error().decode())
            return self._borrow(pointer, 8192, readonly=not writable)

    def observation(self, index=0):
        """Borrow a read-only 144x160 shade buffer; values are 0..3."""
        with self._lock:
            pointer, size = C.POINTER(C.c_uint8)(), C.c_size_t()
            self._check(self._lib.gym_observation_ptr(self._handle(), self._index(index), C.byref(pointer), C.byref(size)))
            if size.value != 23040:
                raise RuntimeError("native observation size mismatch")
            return self._borrow(pointer, size.value).reshape(144, 160)

    def observations(self):
        """Explicit contiguous copy for callers that need an N,H,W tensor."""
        with self._lock:
            return np.stack([self.observation(i) for i in range(self.count)])

    def step(self, actions):
        with self._lock:
            actions = np.asarray(actions)
            if actions.shape != (self.count,) or not np.issubdtype(actions.dtype, np.integer):
                raise ValueError("actions must be one integer byte per environment")
            if np.any(actions < 0) or np.any(actions > 255):
                raise ValueError("action masks must be in 0..255")
            actions = np.ascontiguousarray(actions, dtype=np.uint8)
            self._check(self._lib.gym_step_checked(self._handle(), actions.ctypes.data_as(C.POINTER(C.c_uint8)),
                self.count, self._rewards.ctypes.data_as(C.POINTER(C.c_float)),
                self._dones.ctypes.data_as(C.POINTER(C.c_uint8))))
            return self._rewards.copy(), self._dones.astype(bool)

    def reset(self, index=None):
        with self._lock:
            self._check(self._lib.gym_reset(self._handle(), -1 if index is None else self._index(index)))

    def watch(self, address, *, width=1, scale=1.0, delta=True, signed=False):
        if not isinstance(address, int) or not 0 <= address <= 0xFFFF or width not in (1, 2, 4):
            raise ValueError("invalid watch address or width")
        with self._lock:
            self._check(self._lib.gym_add_watch(self._handle(), address, width, scale, bool(delta), bool(signed)))

    def clear_watches(self):
        with self._lock:
            self._check(self._lib.gym_clear_watches(self._handle()))

    def terminal(self, address=None, *, mask=255, value=0):
        with self._lock:
            if address is None:
                self._check(self._lib.gym_clear_terminal(self._handle()))
            else:
                if not 0 <= address <= 65535 or not 0 <= mask <= 255 or not 0 <= value <= 255:
                    raise ValueError("terminal address, mask or value is out of range")
                self._check(self._lib.gym_set_terminal(self._handle(), address, mask, value))

    def max_steps(self, frames):
        if not isinstance(frames, int) or not 0 <= frames < 2**64:
            raise ValueError("max_steps must be a nonnegative 64-bit integer")
        with self._lock:
            self._check(self._lib.gym_set_max_steps(self._handle(), frames))

    def episode_flags(self, index=0):
        with self._lock:
            terminated, truncated = C.c_uint8(), C.c_uint8()
            self._check(self._lib.gym_episode_flags(self._handle(), self._index(index), C.byref(terminated), C.byref(truncated)))
            return bool(terminated.value), bool(truncated.value)

    def cycles(self, index=0):
        with self._lock:
            return int(self._lib.gym_cycles(self._handle(), self._index(index)))

    def peek(self, address, index=0):
        if not isinstance(address, int) or not 0 <= address <= 65535:
            raise ValueError("address must be a 16-bit integer")
        with self._lock:
            value = C.c_uint8()
            self._check(self._lib.gym_peek(self._handle(), self._index(index), address, C.byref(value)))
            return value.value

    def close(self):
        with self._lock:
            self._storage = None

    def __enter__(self):
        self._handle()
        return self

    def __exit__(self, *_):
        self.close()


class MatchaEnv(gym.Env if gym is not None else object):
    """Gymnasium adapter. See https://gymnasium.farama.org/api/env/.

    Borrowed observations are the default. Set copy_observations=True for replay
    buffers that retain step results without making their own copies.
    """
    metadata = {"render_modes": ["rgb_array"], "render_fps": 4_194_304 / 70_224}

    def __init__(self, rom_path, *, render_mode=None, max_steps=0, copy_observations=False, library=None):
        if gym is None:
            raise ImportError("Install gymnasium to use MatchaEnv; NativeBatch only requires NumPy")
        super().__init__()
        if render_mode not in (None, "rgb_array"):
            raise ValueError("supported render mode: rgb_array")
        self.render_mode = render_mode
        self.copy_observations = copy_observations
        self.action_space = gym.spaces.Discrete(256)
        self.observation_space = gym.spaces.Box(0, 3, shape=(144, 160), dtype=np.uint8)
        self.native = NativeBatch(rom_path, library=library)
        self.native.max_steps(max_steps)
        self._needs_reset = True

    def _observation(self):
        value = self.native.observation()
        return value.copy() if self.copy_observations else value

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        if options:
            raise ValueError("no reset options are currently defined")
        self.native.reset()
        self._needs_reset = False
        return self._observation(), {"t_cycles": self.native.cycles()}

    def step(self, action):
        if self._needs_reset:
            raise RuntimeError("reset() is required before step() or after an episode ends")
        if not self.action_space.contains(action):
            raise ValueError("action must be an integer mask in 0..255")
        rewards, _ = self.native.step([action])
        terminated, truncated = self.native.episode_flags()
        self._needs_reset = terminated or truncated
        return self._observation(), float(rewards[0]), terminated, truncated, {"t_cycles": self.native.cycles()}

    def render(self):
        shades = np.array([255, 170, 85, 0], dtype=np.uint8)[self.native.observation()]
        return np.repeat(shades[:, :, None], 3, axis=2)

    def close(self):
        self.native.close()


if gym is not None and "Matcha-v0" not in gym.registry:
    gym.register(id="Matcha-v0", entry_point="matcha_gym:MatchaEnv")
