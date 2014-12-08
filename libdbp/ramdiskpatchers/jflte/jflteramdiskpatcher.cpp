/*
 * Copyright (C) 2014  Xiao-Long Chen <chenxiaolong@cxl.epac.to>
 *
 * This file is part of MultiBootPatcher
 *
 * MultiBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ramdiskpatchers/jflte/jflteramdiskpatcher.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>

#include "patcherconfig.h"
#include "private/regex.h"
#include "ramdiskpatchers/common/coreramdiskpatcher.h"
#include "ramdiskpatchers/galaxy/galaxyramdiskpatcher.h"
#include "ramdiskpatchers/qcom/qcomramdiskpatcher.h"


/*! \cond INTERNAL */
class JflteBaseRamdiskPatcher::Impl
{
public:
    const PatcherConfig *pc;
    const FileInfo *info;
    CpioFile *cpio;

    std::string getwVersion;

    PatcherError error;
};
/*! \endcond */


static const std::string InitRc = "init.rc";
static const std::string InitTargetRc = "init.target.rc";
static const std::string UeventdRc = "ueventd.rc";
static const std::string UeventdQcomRc = "ueventd.qcom.rc";
static const std::string Msm8960LpmRc = "MSM8960_lpm.rc";


/*!
    \class JflteRamdiskPatcher
    \brief Handles common ramdisk patching operations for the Samsung Galaxy S 4

    This patcher handles the patching of ramdisks for the Samsung Galaxy S 4.
    The currently supported ramdisk types are:

    1. AOSP or AOSP-derived ramdisks
    2. Google Edition (Google Play Edition) ramdisks
    3. TouchWiz (Android 4.2-4.4) ramdisks
 */


JflteBaseRamdiskPatcher::JflteBaseRamdiskPatcher(const PatcherConfig * const pc,
                                                 const FileInfo * const info,
                                                 CpioFile * const cpio)
    : m_impl(new Impl())
{
    m_impl->pc = pc;
    m_impl->info = info;
    m_impl->cpio = cpio;
}

JflteBaseRamdiskPatcher::~JflteBaseRamdiskPatcher()
{
}

PatcherError JflteBaseRamdiskPatcher::error() const
{
    return m_impl->error;
}

////////////////////////////////////////////////////////////////////////////////

const std::string JflteAOSPRamdiskPatcher::Id = "jflte/AOSP/AOSP";

JflteAOSPRamdiskPatcher::JflteAOSPRamdiskPatcher(const PatcherConfig * const pc,
                                                 const FileInfo *const info,
                                                 CpioFile *const cpio)
    : JflteBaseRamdiskPatcher(pc, info, cpio)
{
}

std::string JflteAOSPRamdiskPatcher::id() const
{
    return Id;
}

bool JflteAOSPRamdiskPatcher::patchRamdisk()
{
    CoreRamdiskPatcher corePatcher(m_impl->pc, m_impl->info, m_impl->cpio);
    QcomRamdiskPatcher qcomPatcher(m_impl->pc, m_impl->info, m_impl->cpio);

    if (!corePatcher.patchRamdisk()) {
        m_impl->error = corePatcher.error();
        return false;
    }

    if (!qcomPatcher.addMissingCacheInFstab(std::vector<std::string>())) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.stripManualCacheMounts("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.useGeneratedFstab("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

const std::string JflteGoogleEditionRamdiskPatcher::Id
        = "jflte/GoogleEdition/GoogleEdition";

JflteGoogleEditionRamdiskPatcher::JflteGoogleEditionRamdiskPatcher(const PatcherConfig * const pc,
                                                                   const FileInfo *const info,
                                                                   CpioFile *const cpio)
    : JflteBaseRamdiskPatcher(pc, info, cpio)
{
    if (m_impl->cpio->exists(Msm8960LpmRc)) {
        m_impl->getwVersion = GalaxyRamdiskPatcher::JellyBean;
    } else {
        m_impl->getwVersion = GalaxyRamdiskPatcher::KitKat;
    }
}

std::string JflteGoogleEditionRamdiskPatcher::id() const
{
    return Id;
}

bool JflteGoogleEditionRamdiskPatcher::patchRamdisk()
{
    CoreRamdiskPatcher corePatcher(m_impl->pc, m_impl->info, m_impl->cpio);
    QcomRamdiskPatcher qcomPatcher(m_impl->pc, m_impl->info, m_impl->cpio);
    GalaxyRamdiskPatcher galaxyPatcher(m_impl->pc, m_impl->info, m_impl->cpio,
                                       m_impl->getwVersion);

    if (!corePatcher.patchRamdisk()) {
        m_impl->error = corePatcher.error();
        return false;
    }

    if (!geChargerModeMount()) {
        return false;
    }

    if (!qcomPatcher.addMissingCacheInFstab(std::vector<std::string>())) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.stripManualCacheMounts("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.useGeneratedFstab("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!galaxyPatcher.getwModifyMsm8960LpmRc()) {
        m_impl->error = galaxyPatcher.error();
        return false;
    }

    return true;
}

bool JflteGoogleEditionRamdiskPatcher::geChargerModeMount()
{
    auto contents = m_impl->cpio->contents(InitRc);
    if (contents.empty()) {
        m_impl->error = PatcherError::createCpioError(
                MBP::ErrorCode::CpioFileNotExistError, InitRc);
        return false;
    }

    std::string previousLine;

    std::string strContents(contents.begin(), contents.end());
    std::vector<std::string> lines;
    boost::split(lines, strContents, boost::is_any_of("\n"));

    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (MBP_regex_search(*it, MBP_regex("mount.*/system"))
                && MBP_regex_search(previousLine, MBP_regex("on\\s+charger"))) {
            it = lines.insert(it, "    start mbtool-charger");
            ++it;
            *it = "    wait /.fstab.jgedlte.completed 15";
        }

        previousLine = *it;
    }

    lines.push_back("service mbtool-charger /mbtool mount_fstab /fstab.jgedlte");
    lines.push_back("    class core");
    lines.push_back("    critical");
    lines.push_back("    oneshot");

    strContents = boost::join(lines, "\n");
    contents.assign(strContents.begin(), strContents.end());
    m_impl->cpio->setContents(InitRc, std::move(contents));

    return true;
}

////////////////////////////////////////////////////////////////////////////////

const std::string JflteTouchWizRamdiskPatcher::Id = "jflte/TouchWiz/TouchWiz";

JflteTouchWizRamdiskPatcher::JflteTouchWizRamdiskPatcher(const PatcherConfig * const pc,
                                                         const FileInfo *const info,
                                                         CpioFile *const cpio)
    : JflteBaseRamdiskPatcher(pc, info, cpio)
{
    if (m_impl->cpio->exists(Msm8960LpmRc)) {
        m_impl->getwVersion = GalaxyRamdiskPatcher::JellyBean;
    } else {
        m_impl->getwVersion = GalaxyRamdiskPatcher::KitKat;
    }
}

std::string JflteTouchWizRamdiskPatcher::id() const
{
    return Id;
}

bool JflteTouchWizRamdiskPatcher::patchRamdisk()
{
    CoreRamdiskPatcher corePatcher(m_impl->pc, m_impl->info, m_impl->cpio);
    QcomRamdiskPatcher qcomPatcher(m_impl->pc, m_impl->info, m_impl->cpio);
    GalaxyRamdiskPatcher galaxyPatcher(m_impl->pc, m_impl->info, m_impl->cpio,
                                       m_impl->getwVersion);

    if (!corePatcher.patchRamdisk()) {
        m_impl->error = corePatcher.error();
        return false;
    }

    if (!qcomPatcher.addMissingCacheInFstab(std::vector<std::string>())) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.stripManualCacheMounts("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!qcomPatcher.useGeneratedFstab("init.target.rc")) {
        m_impl->error = qcomPatcher.error();
        return false;
    }

    if (!galaxyPatcher.getwModifyMsm8960LpmRc()) {
        m_impl->error = galaxyPatcher.error();
        return false;
    }

    return true;
}