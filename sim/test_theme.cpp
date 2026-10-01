// The line a theme arrives as (src/theme_custom.h): what is taken, what is refused,
// and what a name becomes.
//   export PATH=~/.platformio/packages/toolchain-gccmingw32/bin:$PATH
//   g++ -O2 -std=gnu++14 -I src sim/test_theme.cpp -o test_theme.exe -static
// (Smart App Control blocks new programs on this PC: run it on the laptop.)
//
//   test_theme.exe            the checks below
//   test_theme.exe FILE       each line of FILE parsed and printed back, for comparing
//                             with what the website made (tools/theme_atlas.py --lines)
#include <cstdio>
#include <cstring>
#include <string>
#include "theme_custom.h"

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static const char* HEX90 = "060a090b120e16241c3dffa81f6f4eb9d4c65f8074e6b955ff5a5ae8f2ed161d1a0f3d334da3ff122a213a3012";

static bool parses(const std::string& s, CustomTheme* out = nullptr, const char** why = nullptr) {
  CustomTheme t;
  const bool ok = themeline::parse(s.c_str(), out ? *out : t, why);
  return ok;
}
static std::string name(const char* in) {
  char out[CustomTheme::NAME_LEN + 1];
  themeline::cleanName(in, out);
  return out;
}

int main(int argc, char** argv) {
  if (argc > 1) {                                   // lines from the website, parsed and printed back
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("can't open %s\n", argv[1]); return 2; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      size_t n = strlen(line);
      while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
      if (!n) continue;
      CustomTheme t;
      const char* why = "";
      const char* p = !strncmp(line, "theme-set ", 10) ? line + 10 : line;
      if (!themeline::parse(p, t, &why)) { printf("REFUSED %s\n", why); continue; }
      char out[themeline::LINE_LEN + 4];
      themeline::format(t, out, sizeof(out));
      printf("%s\n", out);
    }
    fclose(f);
    return 0;
  }

  printf("a good line\n");
  CustomTheme t;
  check(parses(std::string("1 3 ") + HEX90 + " Sunset", &t), "is taken");
  check(t.look == 3 && !strcmp(t.name, "Sunset"), "look and name");
  check(t.colour[0] == 0x060a09 && t.colour[3] == 0x3dffa8 && t.colour[14] == 0x3a3012, "first, fourth and last colour");
  { CustomTheme u; std::string up = HEX90; for (char& c : up) c = (char)toupper(c);
    check(parses("1 0 " + up + " x", &u) && u.colour[3] == 0x3dffa8, "capital hex digits"); }
  { char out[themeline::LINE_LEN + 4]; themeline::format(t, out, sizeof(out));
    CustomTheme u; check(parses(out, &u) && !memcmp(u.colour, t.colour, sizeof(t.colour)) && u.look == t.look && !strcmp(u.name, t.name), "what it lists back parses to the same theme");
    check(std::string(out) == std::string("1 3 ") + HEX90 + " Sunset", "and is the same line"); }

  printf("refused\n");
  const char* why = "";
  check(!parses(std::string("2 3 ") + HEX90 + " x", nullptr, &why), "another version");
  check(!parses(std::string("1 4 ") + HEX90 + " x") && !parses(std::string("1 x ") + HEX90 + " x"), "a look that isn't 0-3");
  check(!parses(std::string("1 3 ") + std::string(HEX90).substr(0, 89) + " x"), "89 hex digits");
  check(!parses(std::string("1 3 ") + HEX90 + "0 x"), "91 hex digits");
  check(!parses(std::string("1 3 ") + std::string(HEX90).substr(0, 40) + "zz" + std::string(HEX90).substr(42) + " x"), "a digit that isn't hex");
  check(!parses("") && !parses("1") && !parses("1 3") && !parses("1 3 ") && !parses(std::string("13 ") + HEX90), "empty and cut short");
  check(!parses(std::string("1  3 ") + HEX90 + " x"), "two spaces where one belongs");

  printf("names\n");
  check(parses(std::string("1 0 ") + HEX90, &t) && !strcmp(t.name, "My theme"), "no name at all: My theme");
  check(parses(std::string("1 0 ") + HEX90 + " ", &t) && !strcmp(t.name, "My theme"), "a name of spaces: My theme");
  check(name("  Deep   Forest  ") == "Deep Forest", "spaces at the ends go, runs of them become one");
  check(name("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmnopqrst" && name("abcdefghijklmnopqrst").size() == 20, "twenty characters at most");
  check(name("abcdefghijklmnopqrs tuv") == "abcdefghijklmnopqrs", "no space left hanging at the cut");
  check(name("Caf\xc3\xa9 \xf0\x9f\x8c\xb2 night") == "Caf? ? night", "an accent and an emoji become one ? each");
  check(name("a\x01" "b\x7f" "c\tok") == "a?b?c ok", "control characters become ?, a tab a space");
  check(name("it's | \"mine\" <3 %s") == "it's | \"mine\" <3 %s", "quotes, bars, brackets and a %s are kept as they are");
  check(name("\x80\x80\x80") == "?", "stray bytes");
  { std::string longUtf; for (int i = 0; i < 40; i++) longUtf += "\xf0\x9f\x98\x80";
    check(name(longUtf.c_str()) == std::string(20, '?'), "forty emoji: twenty ?, and nothing written past the end"); }

  printf("\n%s\n", failures ? "FAILED" : "ALL PASS");
  return failures ? 1 : 0;
}
