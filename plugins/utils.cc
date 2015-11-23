/* utils.cc
   Mathieu Stefani, 23 novembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
*/

#include "utils.h"
#include <sstream>
#include <jml/arch/exception.h>

using namespace std;

namespace Jamloop {
    string urldecode(const std::string& url) {
        auto fromHex = [](char c) {
            if (isdigit(c)) return c - '0';
            switch (c) {
                case 'a':
                case 'A':
                    return 10;
                case 'b':
                case 'B':
                    return 11;
                case 'c':
                case 'C':
                    return 12;
                case 'd':
                case 'D':
                    return 13;
                case 'e':
                case 'E':
                    return 14;
                case 'f':
                case 'F':
                    return 15;
            }

            throw ML::Exception("Invalid hexadecimal character '%c'", c);
        };

        std::ostringstream decoded;
        auto it = url.begin(), end = url.end();
        while (it != end) {
            const char c = *it;
            if (c == '%') {
                if (it[1] && it[2]) {
                    decoded << static_cast<char>(fromHex(it[1]) << 4 | fromHex(it[2]));
                    it += 3;
                }
                else {
                    throw ML::Exception("Unexpected EOF when decoding hexademical character, url='%s'", url.c_str());
                }
            }
            else {
                decoded << c;
                ++it;
            }
        }

        return decoded.str();

    }

} // namespace JamLoop
