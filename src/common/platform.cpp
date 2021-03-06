/* xoreos - A reimplementation of BioWare's Aurora engine
 *
 * xoreos is the legal property of its developers, whose names
 * can be found in the AUTHORS file distributed with this source
 * distribution.
 *
 * xoreos is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * xoreos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with xoreos. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  Platform-dependant functions, mostly for internal use in the Common namespace.
 */

#include "src/common/system.h"

#if defined(WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <shellapi.h>
	#include <wchar.h>
#endif

#if defined(UNIX)
	#include <pwd.h>
	#include <unistd.h>
#endif

#include <cassert>
#include <cstdlib>

#include <memory>

#include <boost/locale.hpp>
#include <boost/filesystem/path.hpp>

#include "src/common/platform.h"
#include "src/common/error.h"
#include "src/common/encoding.h"
#include "src/common/filepath.h"

namespace Common {

void Platform::init() {
	/* Imbue Boost.Filesystem with a locale converting between UTF-8 and the
	 * local encoding. This allows us to work with UTF-8 everywhere. */

	try {
		boost::filesystem::path::imbue(boost::locale::generator().generate(""));
	} catch (std::exception &se) {
		throw Exception(se);
	}
}

// .--- getParameters() ---.
#if defined(WIN32)

/* On Windows, we're not going to use the passed-in argc and argv, since those are
 * usually in a local 8-bit encoding. Instead, we're calling Windows functions to
 * get the parameters in UTF-16, and convert them. */
void Platform::getParameters(int UNUSED(argc), char **UNUSED(argv), std::vector<UString> &args) {
	int argc;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);

	args.clear();
	if (argc <= 0)
		return;

	args.reserve(argc);

	for (int i = 0; i < argc; i++)
		args.push_back(readString(reinterpret_cast<const byte *>(argv[i]), wcslen(argv[i]) * 2, kEncodingUTF16LE));
}

#else

/* On non-Windows system, we assume the parameters are already in UTF-8. */
void Platform::getParameters(int argc, char **argv, std::vector<UString> &args) {
	args.clear();
	if (argc <= 0)
		return;

	args.reserve(argc);

	for (int i = 0; i < argc; i++)
		args.push_back(argv[i]);
}

#endif
// '--- getParameters() ---'

// .--- openFile() ---.
std::FILE *Platform::openFile(const UString &fileName, FileMode mode) {
	assert(((uint) mode) < kFileModeMAX);

	std::FILE *file = 0;

#if defined(WIN32)
	static const wchar_t * const modeStrings[kFileModeMAX] = { L"rb", L"wb" };

	file = _wfopen(boost::filesystem::path(fileName.c_str()).c_str(), modeStrings[(uint) mode]);
#else
	static const char * const modeStrings[kFileModeMAX] = { "rb", "wb" };

	file = std::fopen(boost::filesystem::path(fileName.c_str()).c_str(), modeStrings[(uint) mode]);
#endif

	return file;
}
// '--- openFile() ---'

// .--- Windows utility functions ---.
#if defined(WIN32)

static inline UString getWindowsVariable(const wchar_t *variable) {
	DWORD length = GetEnvironmentVariableW(variable, 0, 0);
	if (!length)
		return "";

	const size_t size = length * sizeof(wchar_t);
	std::unique_ptr<byte[]> data = std::make_unique<byte[]>(size);

	DWORD newLength = GetEnvironmentVariableW(variable, reinterpret_cast<wchar_t *>(data.get()), length);
	if (!newLength || (newLength > length))
		return "";

	return readString(data.get(), size, kEncodingUTF16LE);
}

#endif
// '--- Windows utility functions ---'

// .--- OS-specific directories ---.
UString Platform::getHomeDirectory() {
	UString directory;

#if defined(WIN32)
	// Windows: $USERPROFILE

	directory = getWindowsVariable(L"USERPROFILE");

#elif defined(UNIX)
	// Default Unixoid: $HOME. As a fallback, search the passwd file

	const char *pathStr = getenv("HOME");
	if (!pathStr) {
		struct passwd *pwd = getpwuid(getuid());
		if (pwd)
			pathStr = pwd->pw_dir;
	}

	if (pathStr)
		directory = pathStr;

#endif

	return directory;
}

UString Platform::getConfigDirectory() {
	UString directory;

#if defined(WIN32)
	// Windows: $APPDATA/xoreos/ or $USERPROFILE/xoreos/ or ./

	// Try the Application Data directory
	directory = getWindowsVariable(L"APPDATA");
	if (!directory.empty())
		directory += "\\xoreos";

	// Try the User Profile directory
	if (directory.empty()) {
		directory = getWindowsVariable(L"USERPROFILE");
		if (!directory.empty())
			directory += "\\xoreos";
	}

	// If all else fails (or the Windows version is too low), use the current directory
	if (directory.empty())
		directory = ".";

#elif defined(MACOSX)
	// Mac OS X: ~/Library/Preferences/xoreos/

	directory = getHomeDirectory();
	if (!directory.empty())
		directory += "/Library/Preferences/xoreos";

	if (directory.empty())
		directory = ".";

#elif defined(UNIX)
	// Default Unixoid: $XDG_CONFIG_HOME/xoreos/ or ~/.config/xoreos/

	const char *pathStr = getenv("XDG_CONFIG_HOME");
	if (pathStr) {
		directory = UString(pathStr) + "/xoreos";
	} else {
		directory = getHomeDirectory();
		if (!directory.empty())
			directory += "/.config/xoreos";
	}

	if (directory.empty())
		directory = ".";

#else
	// Fallback: Current directory

	directory = ".";
#endif

	return FilePath::canonicalize(directory);
}

UString Platform::getUserDataDirectory() {
	UString directory;

#if defined(WIN32)
	// Windows: Same as getConfigDirectory()
	directory = getConfigDirectory();
#elif defined(MACOSX)
	// Mac OS X: ~/Library/Application\ Support/xoreos/

	directory = getHomeDirectory();
	if (!directory.empty())
		directory += "/Library/Application Support/xoreos";

	if (directory.empty())
		directory = ".";

#elif defined(UNIX)
	// Default Unixoid: $XDG_DATA_HOME/xoreos/ or ~/.local/share/xoreos/

	const char *pathStr = getenv("XDG_DATA_HOME");
	if (pathStr) {
		directory = UString(pathStr) + "/xoreos";
	} else {
		directory = getHomeDirectory();
		if (!directory.empty())
			directory += "/.local/share/xoreos";
	}

	if (directory.empty())
		directory = ".";

#else
	// Fallback: Same as getConfigDirectory()
	directory = getConfigDirectory();
#endif

	return FilePath::canonicalize(directory);
}
// '--- OS-specific directories ---'

} // End of namespace Common
