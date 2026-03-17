/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

public class SimdMinMaxSmall {

  static byte[] bytes;
  static short[] shorts;
  static char[] chars;

  /// CHECK-START: void SimdMinMaxSmall.minByte(byte) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.minByte(byte) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMin   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void minByte(byte x) {
    for (int i = 0; i < 128; ++i) {
      byte value = bytes[i];
      bytes[i] = value < x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.maxByte(byte) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.maxByte(byte) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMax   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void maxByte(byte x) {
    for (int i = 0; i < 128; ++i) {
      byte value = bytes[i];
      bytes[i] = value > x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.minShort(short) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.minShort(short) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMin   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void minShort(short x) {
    for (int i = 0; i < 128; ++i) {
      short value = shorts[i];
      shorts[i] = value < x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.maxShort(short) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.maxShort(short) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMax   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void maxShort(short x) {
    for (int i = 0; i < 128; ++i) {
      short value = shorts[i];
      shorts[i] = value > x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.minChar(char) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.minChar(char) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMin   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void minChar(char x) {
    for (int i = 0; i < 128; ++i) {
      char value = chars[i];
      chars[i] = value < x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.maxChar(char) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.maxChar(char) loop_optimization (after)
  /// CHECK-DAG: VecLoad  loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecMax   loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore loop:<<Loop>>      outer_loop:none
  static void maxChar(char x) {
    for (int i = 0; i < 128; ++i) {
      char value = chars[i];
      chars[i] = value > x ? value : x;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satAddByte(byte) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satAddByte(byte) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationAdd loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satAddByte(byte x) {
    for (int i = 0; i < 128; ++i) {
      int sum = bytes[i] + x;
      if (sum > Byte.MAX_VALUE) {
        sum = Byte.MAX_VALUE;
      } else if (sum < Byte.MIN_VALUE) {
        sum = Byte.MIN_VALUE;
      }
      bytes[i] = (byte) sum;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satSubByte(byte) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satSubByte(byte) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationSub loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satSubByte(byte x) {
    for (int i = 0; i < 128; ++i) {
      int diff = bytes[i] - x;
      if (diff > Byte.MAX_VALUE) {
        diff = Byte.MAX_VALUE;
      } else if (diff < Byte.MIN_VALUE) {
        diff = Byte.MIN_VALUE;
      }
      bytes[i] = (byte) diff;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satAddShort(short) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satAddShort(short) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationAdd loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satAddShort(short x) {
    for (int i = 0; i < 128; ++i) {
      int sum = shorts[i] + x;
      if (sum > Short.MAX_VALUE) {
        sum = Short.MAX_VALUE;
      } else if (sum < Short.MIN_VALUE) {
        sum = Short.MIN_VALUE;
      }
      shorts[i] = (short) sum;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satSubShort(short) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satSubShort(short) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationSub loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satSubShort(short x) {
    for (int i = 0; i < 128; ++i) {
      int diff = shorts[i] - x;
      if (diff > Short.MAX_VALUE) {
        diff = Short.MAX_VALUE;
      } else if (diff < Short.MIN_VALUE) {
        diff = Short.MIN_VALUE;
      }
      shorts[i] = (short) diff;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satAddChar(char) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satAddChar(char) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationAdd loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satAddChar(char x) {
    for (int i = 0; i < 128; ++i) {
      int sum = chars[i] + x;
      if (sum > Character.MAX_VALUE) {
        sum = Character.MAX_VALUE;
      }
      chars[i] = (char) sum;
    }
  }

  /// CHECK-START: void SimdMinMaxSmall.satSubChar(char) loop_optimization (before)
  /// CHECK-DAG: ArrayGet loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: ArraySet loop:<<Loop>>      outer_loop:none
  ///
  /// CHECK-START-LOONGARCH64: void SimdMinMaxSmall.satSubChar(char) loop_optimization (after)
  /// CHECK-DAG: VecLoad          loop:<<Loop:B\d+>> outer_loop:none
  /// CHECK-DAG: VecSaturationSub loop:<<Loop>>      outer_loop:none
  /// CHECK-DAG: VecStore         loop:<<Loop>>      outer_loop:none
  static void satSubChar(char x) {
    for (int i = 0; i < 128; ++i) {
      int diff = chars[i] - x;
      if (diff < 0) {
        diff = 0;
      }
      chars[i] = (char) diff;
    }
  }

  public static void main() {
    bytes = new byte[128];
    for (int i = 0; i < 128; ++i) {
      bytes[i] = (byte) (i - 64);
    }
    minByte((byte) -10);
    for (int i = 0; i < 128; ++i) {
      expectEquals((byte) Math.min(i - 64, -10), bytes[i], "minByte");
    }
    maxByte((byte) 10);
    for (int i = 0; i < 128; ++i) {
      expectEquals((byte) Math.max(Math.min(i - 64, -10), 10), bytes[i], "maxByte");
    }

    for (int i = 0; i < 128; ++i) {
      bytes[i] = (byte) (i - 64);
    }
    satAddByte((byte) 100);
    for (int i = 0; i < 128; ++i) {
      expectEquals(
          Math.max(Byte.MIN_VALUE, Math.min(i - 64 + 100, Byte.MAX_VALUE)),
          bytes[i],
          "satAddByte");
    }

    for (int i = 0; i < 128; ++i) {
      bytes[i] = (byte) (i - 64);
    }
    satSubByte((byte) 100);
    for (int i = 0; i < 128; ++i) {
      expectEquals(
          Math.max(Byte.MIN_VALUE, Math.min(i - 64 - 100, Byte.MAX_VALUE)),
          bytes[i],
          "satSubByte");
    }

    shorts = new short[128];
    for (int i = 0; i < 128; ++i) {
      shorts[i] = (short) (i - 64);
    }
    minShort((short) -10);
    for (int i = 0; i < 128; ++i) {
      expectEquals((short) Math.min(i - 64, -10), shorts[i], "minShort");
    }
    maxShort((short) 10);
    for (int i = 0; i < 128; ++i) {
      expectEquals((short) Math.max(Math.min(i - 64, -10), 10), shorts[i], "maxShort");
    }

    for (int i = 0; i < 128; ++i) {
      shorts[i] = (short) (i * 256 - 16000);
    }
    satAddShort((short) 30000);
    for (int i = 0; i < 128; ++i) {
      expectEquals(
          Math.max(Short.MIN_VALUE, Math.min(i * 256 - 16000 + 30000, Short.MAX_VALUE)),
          shorts[i],
          "satAddShort");
    }

    for (int i = 0; i < 128; ++i) {
      shorts[i] = (short) (i * 256 - 16000);
    }
    satSubShort((short) 30000);
    for (int i = 0; i < 128; ++i) {
      expectEquals(
          Math.max(Short.MIN_VALUE, Math.min(i * 256 - 16000 - 30000, Short.MAX_VALUE)),
          shorts[i],
          "satSubShort");
    }

    chars = new char[128];
    for (int i = 0; i < 128; ++i) {
      chars[i] = (char) (i * 3);
    }
    minChar((char) 100);
    for (int i = 0; i < 128; ++i) {
      expectEquals((char) Math.min(i * 3, 100), chars[i], "minChar");
    }
    maxChar((char) 280);
    for (int i = 0; i < 128; ++i) {
      expectEquals((char) Math.max(Math.min(i * 3, 100), 280), chars[i], "maxChar");
    }

    for (int i = 0; i < 128; ++i) {
      chars[i] = (char) (65400 + i);
    }
    satAddChar((char) 500);
    for (int i = 0; i < 128; ++i) {
      expectEquals(Math.min(65400 + i + 500, Character.MAX_VALUE), chars[i], "satAddChar");
    }

    for (int i = 0; i < 128; ++i) {
      chars[i] = (char) (i * 17);
    }
    satSubChar((char) 500);
    for (int i = 0; i < 128; ++i) {
      expectEquals(Math.max(i * 17 - 500, 0), chars[i], "satSubChar");
    }

    System.out.println("SimdMinMaxSmall passed");
  }

  private static void expectEquals(int expected, int result, String action) {
    if (expected != result) {
      throw new Error("Expected: " + expected + ", found: " + result + " for " + action);
    }
  }
}
