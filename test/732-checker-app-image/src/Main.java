/*
 * Copyright (C) 2024 The Android Open Source Project
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

public class Main {
    public static void main(String args[]) {
        System.out.println($noinline$getAppImageClass().getName());
        System.out.println($noinline$getNonAppImageClass().getName());

        $noinline$callAppImageClassNop();
        $noinline$callAppImageClassWithClinitNop();
        $noinline$callNonAppImageClassNop();
    }

    /// CHECK-START: java.lang.Class Main.$noinline$getAppImageClass() builder (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:true
    public static Class<?> $noinline$getAppImageClass() {
        return AppImageClass.class;
    }

    /// CHECK-START: java.lang.Class Main.$noinline$getNonAppImageClass() builder (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:false
    public static Class<?> $noinline$getNonAppImageClass() {
        return NonAppImageClass.class;
    }

    /// CHECK-START: void Main.$noinline$callAppImageClassNop() builder (after)
    /// CHECK:            InvokeStaticOrDirect clinit_check:none

    /// CHECK-START: void Main.$noinline$callAppImageClassNop() builder (after)
    /// CHECK-NOT:        LoadClass
    /// CHECK-NOT:        ClinitCheck

    /// CHECK-START: void Main.$noinline$callAppImageClassNop() inliner (after)
    /// CHECK-NOT:        LoadClass
    /// CHECK-NOT:        ClinitCheck
    /// CHECK-NOT:        InvokeStaticOrDirect
    public static void $noinline$callAppImageClassNop() {
        AppImageClass.$inline$nop();
    }

    /// CHECK-START: void Main.$noinline$callAppImageClassWithClinitNop() builder (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:true gen_clinit_check:false
    /// CHECK:            ClinitCheck
    /// CHECK:            InvokeStaticOrDirect clinit_check:explicit

    /// CHECK-START: void Main.$noinline$callAppImageClassWithClinitNop() inliner (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:true gen_clinit_check:false
    /// CHECK:            ClinitCheck

    /// CHECK-START: void Main.$noinline$callAppImageClassWithClinitNop() inliner (after)
    /// CHECK-NOT:        InvokeStaticOrDirect

    /// CHECK-START: void Main.$noinline$callAppImageClassWithClinitNop() prepare_for_register_allocation (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:true gen_clinit_check:true

    /// CHECK-START: void Main.$noinline$callAppImageClassWithClinitNop() prepare_for_register_allocation (after)
    /// CHECK-NOT:        ClinitCheck
    public static void $noinline$callAppImageClassWithClinitNop() {
        AppImageClassWithClinit.$inline$nop();
    }

    /// CHECK-START: void Main.$noinline$callNonAppImageClassNop() builder (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:false gen_clinit_check:false
    /// CHECK:            ClinitCheck
    /// CHECK:            InvokeStaticOrDirect clinit_check:explicit

    /// CHECK-START: void Main.$noinline$callNonAppImageClassNop() inliner (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:false gen_clinit_check:false
    /// CHECK:            ClinitCheck

    /// CHECK-START: void Main.$noinline$callNonAppImageClassNop() inliner (after)
    /// CHECK-NOT:        InvokeStaticOrDirect

    /// CHECK-START: void Main.$noinline$callNonAppImageClassNop() prepare_for_register_allocation (after)
    /// CHECK:            LoadClass load_kind:BssEntry in_image:false gen_clinit_check:true

    /// CHECK-START: void Main.$noinline$callNonAppImageClassNop() prepare_for_register_allocation (after)
    /// CHECK-NOT:        ClinitCheck
    public static void $noinline$callNonAppImageClassNop() {
        NonAppImageClass.$inline$nop();
    }
}

class AppImageClass {  // Included in the profile.
    public static void $inline$nop() {}
}

class AppImageClassWithClinit {  // Included in the profile.
    static boolean doThrow = false;
    static {
        if (doThrow) {
            throw new Error();
        }
    }

    public static void $inline$nop() {}
}

class NonAppImageClass {  // Not included in the profile.
    public static void $inline$nop() {}
}
