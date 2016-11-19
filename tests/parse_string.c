/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity/examples/unity_config.h"
#include "unity/src/unity.h"
#include "../cJSON.c"

static cJSON item[1];

static const unsigned char *error_pointer = NULL;
static cJSON_Hooks hooks = { malloc, free };

static void assert_is_string(cJSON *string_item)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(string_item, "Item is NULL.");

    TEST_ASSERT_NULL_MESSAGE(string_item->next, "Linked list next pointer is not NULL.");
    TEST_ASSERT_NULL_MESSAGE(string_item->child, "Child pointer is not NULL.");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(cJSON_String, string_item->type, "Item type is not string.");
    TEST_ASSERT_TRUE_MESSAGE(string_item->is_reference, "Item should have a string as reference.");
    TEST_ASSERT_FALSE_MESSAGE(string_item->string_is_const, "Item should not have a const string.");
    TEST_ASSERT_NULL_MESSAGE(string_item->name, "Name is not NULL.");
}

static void assert_parse_string(const char *string, const char *expected)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(parse_string(item, (const unsigned char*)string, &error_pointer, &hooks), "Couldn't parse string.");
    assert_is_string(item);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, item->value.string, "The parsed result isn't as expected.");
    hooks.free_fn(item->value.string);
    item->value.string = NULL;
}

static void parse_string_should_parse_strings(void)
{
    assert_parse_string("\"\"", "");
    assert_parse_string(
        "\" !\\\"#$%&'()*+,-./\\/0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\\\]^_'abcdefghijklmnopqrstuvwxyz{|}~\"",
        " !\"#$%&'()*+,-.//0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_'abcdefghijklmnopqrstuvwxyz{|}~");
    assert_parse_string(
        "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u20AC\\u732b\"",
        "\"\\/\b\f\n\r\t€猫");
    assert_parse_string("\"\b\f\n\r\t\"", "\b\f\n\r\t");
}

static void parse_string_should_parse_utf16_surrogate_pairs(void)
{
    assert_parse_string("\"\\uD83D\\udc31\"", "🐱");
}

static void parse_string_should_not_parse_non_strings(void)
{
    TEST_ASSERT_NULL(parse_string(item, (const unsigned char*)"this\" is not a string\"", &error_pointer, &hooks));
    TEST_ASSERT_NULL(parse_string(item, (const unsigned char*) "", &error_pointer, &hooks));
}

static void parse_string_should_not_parse_invalid_backslash(void)
{
    TEST_ASSERT_NULL_MESSAGE(parse_string(item, (const unsigned char*)"Abcdef\\123", &error_pointer, &hooks), "Invalid backshlash should not be accepted.");
    TEST_ASSERT_NULL_MESSAGE(parse_string(item, (const unsigned char*)"Abcdef\\e23", &error_pointer, &hooks), "Invalid backshlash should not be accepted.");
}

static void parse_string_should_not_overflow_with_closing_backslash(void)
{
    TEST_ASSERT_NULL_MESSAGE(parse_string(item, (const unsigned char*)"\"000000000000000000\\", &error_pointer, &hooks), "Malformed string should not be accepted.");
}

static void parse_string_should_parse_bug_94(void)
{
    const char string[] = "\"~!@\\\\#$%^&*()\\\\\\\\-\\\\+{}[]:\\\\;\\\\\\\"\\\\<\\\\>?/.,DC=ad,DC=com\"";
    assert_parse_string(string, "~!@\\#$%^&*()\\\\-\\+{}[]:\\;\\\"\\<\\>?/.,DC=ad,DC=com");
}

int main(void)
{
    /* initialize cJSON item and error pointer */
    memset(item, 0, sizeof(cJSON));

    UNITY_BEGIN();
    RUN_TEST(parse_string_should_parse_strings);
    RUN_TEST(parse_string_should_parse_utf16_surrogate_pairs);
    RUN_TEST(parse_string_should_not_parse_non_strings);
    RUN_TEST(parse_string_should_not_parse_invalid_backslash);
    RUN_TEST(parse_string_should_parse_bug_94);
    RUN_TEST(parse_string_should_not_overflow_with_closing_backslash);
    return UNITY_END();
}
