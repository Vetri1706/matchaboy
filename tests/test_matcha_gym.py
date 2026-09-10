import ctypes as C
import gc
from pathlib import Path
import tempfile
import unittest
import weakref
import numpy as np
import gymnasium as gym
from gymnasium.utils.env_checker import check_env
from matcha_gym import MatchaEnv, NativeBatch


class GymTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.rom = Path(cls.directory.name) / "joypad.gb"
        data = bytearray(32768)
        data[0x100:0x103] = bytes([0xC3, 0x50, 1])
        data[0x150:0x163] = bytes([
            0xF3, 0x31, 0xFE, 0xFF, 0x3E, 0x20, 0xE0, 0x00,
            0xF0, 0x00, 0x2F, 0xE6, 0x0F, 0xEA, 0x00, 0xC0, 0xC3, 0x58, 0x01])
        cls.rom.write_bytes(data)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_batched_actions_rewards_and_zero_copy(self):
        with NativeBatch(self.rom, 16) as batch:
            batch.watch(0xC000, scale=2, delta=False)
            borrowed = batch.ram(3)
            writable = batch.ram(3, writable=True)
            self.assertTrue(np.shares_memory(borrowed, writable))
            self.assertFalse(borrowed.flags.writeable)
            address = borrowed.ctypes.data
            rewards, dones = batch.step(np.arange(16))
            np.testing.assert_array_equal(rewards, np.arange(16) * 2)
            self.assertFalse(dones.any())
            self.assertEqual(borrowed[0], 3)
            self.assertEqual(batch.peek(0xE000, 3), 3)  # Real WRAM echo routing.
            writable[10] = 123
            self.assertEqual(batch.peek(0xC00A, 3), 123)
            batch.reset(3)
            self.assertEqual(batch.ram(3).ctypes.data, address)
            self.assertEqual(borrowed[10], 0)
            self.assertEqual(batch.observation(3).shape, (144, 160))
            self.assertEqual(batch.observations().shape, (16, 144, 160))

    def test_borrowed_storage_lifetime(self):
        batch = NativeBatch(self.rom)
        view = np.asarray(batch.ram())[0:20]
        storage = weakref.ref(batch._storage)
        batch.step([7])
        batch.close()
        gc.collect()
        self.assertIsNotNone(storage())
        self.assertEqual(view[0], 7)
        with self.assertRaises(RuntimeError):
            batch.step([0])
        del view
        gc.collect()
        self.assertIsNone(storage())

    def test_gymnasium_contract(self):
        env = gym.make("Matcha-v0", rom_path=self.rom, copy_observations=True,
                       render_mode="rgb_array").unwrapped
        try:
            check_env(env, skip_render_check=False)
            env.native.watch(0xC000, scale=1, delta=False)
            obs, info = env.reset(seed=42)
            self.assertEqual(info["t_cycles"], 0)
            obs, reward, terminated, truncated, info = env.step(9)
            self.assertEqual(reward, 9)
            self.assertFalse(terminated or truncated)
            self.assertGreaterEqual(info["t_cycles"], 70224)
            self.assertEqual(env.render().shape, (144, 160, 3))
            env.native.max_steps(1)
            env.reset()
            self.assertTrue(env.step(0)[3])
            with self.assertRaises(RuntimeError):
                env.step(0)
        finally:
            env.close()

    def test_errors_cross_abi_as_status(self):
        with NativeBatch(self.rom, 2) as batch:
            with self.assertRaises(ValueError): batch.step([256, 0])
            with self.assertRaises(ValueError): batch.step([0])
            with self.assertRaises(IndexError): batch.ram(-1)
            with self.assertRaises(RuntimeError): batch.watch(0xFFFF, width=2)
            status = batch._lib.gym_step_checked(None, None, 2, None, None)
            self.assertEqual(status, -1)
            self.assertTrue(batch._lib.gym_last_error())
            null = C.POINTER(C.c_uint8)()
            batch._lib.gym_get_ram(batch._handle(), 99, C.byref(null))
            self.assertFalse(null)


if __name__ == "__main__":
    unittest.main(verbosity=2)
