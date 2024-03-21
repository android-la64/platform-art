/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "managed_register_loongarch64.h"

#include "base/globals.h"
#include "gtest/gtest.h"

namespace art {
namespace loongarch64 {

TEST(Loongarch64ManagedRegister, NoRegister) {
  Loongarch64ManagedRegister reg = ManagedRegister::NoRegister().AsLoongarch64();
  EXPECT_TRUE(reg.IsNoRegister());
}

TEST(Loongarch64ManagedRegister, XRegister) {
  Loongarch64ManagedRegister reg = Loongarch64ManagedRegister::FromXRegister(Zero);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(Zero, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(RA);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(RA, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(SP);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(SP, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(A0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(A0, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(A2);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(A2, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(A7);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(A7, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(T0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(T0, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(T9);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(T9, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(S9);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(S9, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(S0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(S0, reg.AsXRegister());

  reg = Loongarch64ManagedRegister::FromXRegister(S8);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_TRUE(reg.IsXRegister());
  EXPECT_FALSE(reg.IsFRegister());
  EXPECT_EQ(S8, reg.AsXRegister());
}

TEST(Loongarch64ManagedRegister, FRegister) {
  Loongarch64ManagedRegister reg = Loongarch64ManagedRegister::FromFRegister(FA0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FA0, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FA0)));

  reg = Loongarch64ManagedRegister::FromFRegister(FA7);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FA7, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FA7)));

  reg = Loongarch64ManagedRegister::FromFRegister(FT0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FT0, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));

  reg = Loongarch64ManagedRegister::FromFRegister(FT9);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FT9, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FT9)));

  reg = Loongarch64ManagedRegister::FromFRegister(FT15);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FT15, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FT15)));

  reg = Loongarch64ManagedRegister::FromFRegister(FS0);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FS0, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FS0)));

  reg = Loongarch64ManagedRegister::FromFRegister(FS7);
  EXPECT_FALSE(reg.IsNoRegister());
  EXPECT_FALSE(reg.IsXRegister());
  EXPECT_TRUE(reg.IsFRegister());
  EXPECT_EQ(FS7, reg.AsFRegister());
  EXPECT_TRUE(reg.Equals(Loongarch64ManagedRegister::FromFRegister(FS7)));
}

TEST(Loongarch64ManagedRegister, Equals) {
  ManagedRegister no_reg = ManagedRegister::NoRegister();
  EXPECT_TRUE(no_reg.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_FALSE(no_reg.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(no_reg.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(no_reg.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_FALSE(no_reg.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(no_reg.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));

  Loongarch64ManagedRegister reg_Zero = Loongarch64ManagedRegister::FromXRegister(Zero);
  EXPECT_FALSE(reg_Zero.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_TRUE(reg_Zero.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(reg_Zero.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(reg_Zero.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_FALSE(reg_Zero.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(reg_Zero.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));

  Loongarch64ManagedRegister reg_A1 = Loongarch64ManagedRegister::FromXRegister(A1);
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::FromXRegister(A0)));
  EXPECT_TRUE(reg_A1.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(reg_A1.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));

  Loongarch64ManagedRegister reg_S2 = Loongarch64ManagedRegister::FromXRegister(S2);
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::FromXRegister(S1)));
  EXPECT_TRUE(reg_S2.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(reg_S2.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));

  Loongarch64ManagedRegister reg_F0 = Loongarch64ManagedRegister::FromFRegister(FT0);
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_TRUE(reg_F0.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::FromFRegister(FT1)));
  EXPECT_FALSE(reg_F0.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));

  Loongarch64ManagedRegister reg_F31 = Loongarch64ManagedRegister::FromFRegister(FT11);
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::NoRegister()));
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::FromXRegister(Zero)));
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::FromXRegister(A1)));
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::FromXRegister(S2)));
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::FromFRegister(FT0)));
  EXPECT_FALSE(reg_F31.Equals(Loongarch64ManagedRegister::FromFRegister(FT1)));
  EXPECT_TRUE(reg_F31.Equals(Loongarch64ManagedRegister::FromFRegister(FT11)));
}

}  // namespace loongarch64
}  // namespace art

