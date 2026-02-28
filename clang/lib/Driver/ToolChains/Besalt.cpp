//===--- Besalt.cpp - Besalt ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Besalt.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

// Besalt linker
void besalt::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const auto &TC = static_cast<const Besalt &>(getToolChain());
  const Driver &D = TC.getDriver();
  const bool IsStatic = Args.hasArg(options::OPT_static);
  const bool IsShared = Args.hasArg(options::OPT_shared);
  const bool IsRelocatable = Args.hasArg(options::OPT_r);
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  // Page size
  CmdArgs.push_back("-z");
  CmdArgs.push_back("max-page-size=4096");

  // Hash style
  CmdArgs.push_back("--hash-style=gnu");

  // Build ID
  CmdArgs.push_back("--build-id");

  if (IsStatic) {
    CmdArgs.push_back("-static");
  } else {
    if (IsShared)
      CmdArgs.push_back("-shared");
    else if (!IsRelocatable) {
      if (Args.hasArg(options::OPT_rdynamic))
        CmdArgs.push_back("-export-dynamic");

      if (Args.hasFlag(options::OPT_pie, options::OPT_no_pie,
                       TC.isPIEDefault(Args)))
        CmdArgs.push_back("-pie");

      CmdArgs.push_back("--dynamic-linker");
      CmdArgs.push_back(Args.MakeArgString(TC.getDynamicLinker(Args)));
    }

    CmdArgs.push_back("--eh-frame-hdr");
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  Args.addAllArgs(CmdArgs,
                  {options::OPT_L, options::OPT_T_Group, options::OPT_u,
                   options::OPT_s, options::OPT_t});
  TC.AddFilePathLibArgs(Args, CmdArgs);

  // Inject start files
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    if (!IsShared)
      CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crt_start.o")));
  }

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  // Inject default libraries
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    if (D.CCCIsCXX())
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
    if (!Args.hasArg(options::OPT_nolibc)) {
      CmdArgs.push_back("-lc");
      CmdArgs.push_back("-lbesalt");
    }
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("core.o")));
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("compiler_builtins.o")));
    AddRunTimeLibs(TC, D, CmdArgs, Args);
  }

  // Inject default linker script for PIE executables.
  // Skip if the caller manages the link themselves (-nostdlib) or provides
  // their own linker script (-T).
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_T) &&
      !IsShared && !IsRelocatable && !IsStatic) {
    CmdArgs.push_back("-T");
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("saltyos-pie.ld")));
  }

  const char *Exec =
      Args.MakeArgString(TC.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

// Besalt toolchain
Besalt::Besalt(const Driver &D, const llvm::Triple &Triple,
                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  if (!D.SysRoot.empty()) {
    SmallString<128> P(D.SysRoot);
    llvm::sys::path::append(P, "usr", "lib");
    getFilePaths().push_back(std::string(P));
  }
}

Tool *Besalt::buildLinker() const {
  return new tools::besalt::Linker(*this);
}

void Besalt::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> ResourceDir(D.ResourceDir);
    llvm::sys::path::append(ResourceDir, "include");
    addSystemInclude(DriverArgs, CC1Args, ResourceDir);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  if (!D.SysRoot.empty()) {
    SmallString<128> P(D.SysRoot);
    llvm::sys::path::append(P, "usr", "include");
    addExternCSystemInclude(DriverArgs, CC1Args, P.str());
  }
}
