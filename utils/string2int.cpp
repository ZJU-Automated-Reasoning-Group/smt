#include "string2int.h"

#include <cerrno>
#include <cstdlib>
#include <limits>

template<typename T>
inline T str2number(const char *str, int base, bool safe) {
	int errno_bak = errno;
	errno = 0;
	char *endptr;
// _strtoi64 is available in Visual Studio, but not yet in MINGW
#ifdef _MSC_VER
    const __int64 val=_strtoi64(str, &endptr, base);
#else
	const long long val = strtoll(str, &endptr, base);
#endif

	if (safe) {
		errno = errno_bak;
		if (std::numeric_limits<T>::min() == 0) {
			// unsigned;
		} else {
			// signed
		}
	}

	return (T) val;
}

unsigned safe_string2unsigned(const std::string &str, int base) {
	return str2number<unsigned>(str.c_str(), base, true);
}

std::size_t safe_string2size_t(const std::string &str, int base) {
	return str2number<std::size_t>(str.c_str(), base, true);
}

int unsafe_string2int(const std::string &str, int base) {
	return str2number<int>(str.c_str(), base, false);
}

unsigned unsafe_string2unsigned(const std::string &str, int base) {
	return str2number<unsigned>(str.c_str(), base, false);
}

std::size_t unsafe_string2size_t(const std::string &str, int base) {
	return str2number<std::size_t>(str.c_str(), base, false);
}

signed long long int unsafe_string2signedlonglong(const std::string &str,
		int base) {
	return str2number<signed long long int>(str.c_str(), base, false);
}

unsigned long long int unsafe_string2unsignedlonglong(const std::string &str,
		int base) {
	return str2number<unsigned long long int>(str.c_str(), base, false);
}
