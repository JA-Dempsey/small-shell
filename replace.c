#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Function based on video posted by Ryan Gambord video:
 * https://www.youtube.com/watch?v=-3ty5W_6-IQ
 * haystack = string being looked through
 * needle = substring to look for
 * sub = what to substitute any found needles for
 * subct = how many times the function will substitute (-1 will go to end of word)
 *
 * Some changes made to work better with how I structured my code
 */
char *str_replace(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub, int subct)
{
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle);
  size_t const sub_len = strlen(sub);

  for (int i = -1; (str = strstr(str, needle)) && (i <= subct); ) {
    ptrdiff_t offset = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + offset;
    }

    memmove(str + sub_len, str + needle_len, haystack_len + 1 - offset - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;

    // If subct < 0 the function will replace every
    // instance of the needle it can find
    if (subct > -1) {
      i++;
    } else {
      i--;
    }
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}

