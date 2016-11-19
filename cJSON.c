/*
  Copyright (c) 2009 Dave Gamble

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

/* cJSON */
/* JSON parser in C. */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include "cJSON.h"

/* define our own boolean type */
typedef int cjbool;
#define true ((cjbool)1)
#define false ((cjbool)0)

static const unsigned char *global_ep = NULL;

const char *cJSON_GetErrorPtr(void)
{
    return (const char*) global_ep;
}

extern const char* cJSON_Version(void)
{
    static char version[15];
    sprintf(version, "%i.%i.%i", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);

    return version;
}

static cJSON_Hooks global_hooks = { malloc, free };

static unsigned char* cJSON_strdup(const unsigned char* str, const cJSON_Hooks * const hooks)
{
    size_t len = 0;
    unsigned char *copy = NULL;

    if (str == NULL)
    {
        return NULL;
    }

    len = strlen((const char*)str) + 1;
    if (!(copy = (unsigned char*)hooks->malloc_fn(len)))
    {
        return NULL;
    }
    memcpy(copy, str, len);

    return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks)
    {
        /* Reset hooks */
        global_hooks.malloc_fn = malloc;
        global_hooks.free_fn = free;
        return;
    }

    global_hooks.malloc_fn = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    global_hooks.free_fn = (hooks->free_fn) ? hooks->free_fn : free;
}

/* Internal constructor. */
static cJSON *cJSON_New_Item(const cJSON_Hooks * const hooks)
{
    cJSON* node = (cJSON*)hooks->malloc_fn(sizeof(cJSON));
    if (node)
    {
        memset(node, '\0', sizeof(cJSON));
    }

    return node;
}

/* Delete a cJSON structure. */
static void internal_cJSON_Delete(cJSON *c, const cJSON_Hooks * const hooks)
{
    cJSON *next = NULL;
    while (c)
    {
        next = c->next;
        if (!c->is_reference && c->child)
        {
            internal_cJSON_Delete(c->child, hooks);
        }
        if (!c->is_reference && c->string)
        {
            hooks->free_fn(c->string);
        }
        if (!c->string_is_const && c->name)
        {
            hooks->free_fn(c->name);
        }
        hooks->free_fn(c);
        c = next;
    }
}
void cJSON_Delete(cJSON *c)
{
    internal_cJSON_Delete(c, &global_hooks);
}

/* Parse the input text to generate a number, and populate the result into item. */
static const unsigned char *parse_number(cJSON *item, const unsigned char *num)
{
    double number = 0;
    unsigned char *endpointer = NULL;

    if (num == NULL)
    {
        return NULL;
    }

    number = strtod((const char*)num, (char**)&endpointer);
    if ((num == endpointer) || (num == NULL))
    {
        /* parse_error */
        return NULL;
    }

    item->number = number;

    item->type = cJSON_Number;

    return endpointer;
}


typedef struct
{
    unsigned char *buffer;
    size_t length;
    size_t offset;
    cjbool noalloc;
} printbuffer;


#define max(a, b) ((a > b) ? a : b)

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char* ensure(printbuffer *p, size_t needed, const cJSON_Hooks * const hooks)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if (!p || !p->buffer)
    {
        return NULL;
    }
    needed += p->offset;
    if (needed <= p->length)
    {
        return p->buffer + p->offset;
    }

    if (p->noalloc) {
        return NULL;
    }

    newsize = max(p->length, needed) * 2;
    if (newsize <= needed) {
        return NULL;
    }

    newbuffer = (unsigned char*)hooks->malloc_fn(newsize);
    if (!newbuffer)
    {
        hooks->free_fn(p->buffer);
        p->length = 0;
        p->buffer = NULL;

        return NULL;
    }
    if (newbuffer)
    {
        memcpy(newbuffer, p->buffer, p->length);
    }
    hooks->free_fn(p->buffer);
    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* calculate the new length of the string in a printbuffer */
static size_t update(const printbuffer *p)
{
    const unsigned char *str = NULL;
    if (!p || !p->buffer)
    {
        return 0;
    }
    str = p->buffer + p->offset;

    return p->offset + strlen((const char*)str);
}

/* Render the number nicely from the given item into a string. */
static unsigned char *print_number(const cJSON *item, printbuffer *p, const cJSON_Hooks * const hooks)
{
    unsigned char *str = NULL;
    double d = item->number;
    /* special case for 0. */
    if (d == 0)
    {
        if (p)
        {
            str = ensure(p, 2, hooks);
        }
        else
        {
            str = (unsigned char*)hooks->malloc_fn(2);
        }
        if (str)
        {
            strcpy((char*)str,"0");
        }
    }
    /* value is an int */
    else if ((fabs(floor(item->number) - d) <= DBL_EPSILON) && (d <= INT_MAX) && (d >= INT_MIN))
    {
        int value = (int)item->number;
        if (p)
        {
            str = ensure(p, 21, hooks);
        }
        else
        {
            /* 2^64+1 can be represented in 21 chars. */
            str = (unsigned char*)hooks->malloc_fn(21);
        }
        if (str)
        {
            sprintf((char*)str, "%d", value);
        }
    }
    /* value is a floating point number */
    else
    {
        if (p)
        {
            /* This is a nice tradeoff. */
            str = ensure(p, 64, hooks);
        }
        else
        {
            /* This is a nice tradeoff. */
            str = (unsigned char*)hooks->malloc_fn(64);
        }
        if (str)
        {
            /* This checks for NaN and Infinity */
            if ((d * 0) != 0)
            {
                sprintf((char*)str, "null");
            }
            else if ((fabs(floor(d) - d) <= DBL_EPSILON) && (fabs(d) < 1.0e60))
            {
                sprintf((char*)str, "%.0f", d);
            }
            else if ((fabs(d) < 1.0e-6) || (fabs(d) > 1.0e9))
            {
                sprintf((char*)str, "%e", d);
            }
            else
            {
                sprintf((char*)str, "%f", d);
            }
        }
    }
    return str;
}

/* parse 4 digit hexadecimal number */
static unsigned parse_hex4(const unsigned char *str)
{
    unsigned int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++)
    {
        /* parse digit */
        if ((*str >= '0') && (*str <= '9'))
        {
            h += (unsigned int) (*str) - '0';
        }
        else if ((*str >= 'A') && (*str <= 'F'))
        {
            h += (unsigned int) 10 + (*str) - 'A';
        }
        else if ((*str >= 'a') && (*str <= 'f'))
        {
            h += (unsigned int) 10 + (*str) - 'a';
        }
        else /* invalid */
        {
            return 0;
        }

        if (i < 3)
        {
            /* shift left to make place for the next nibble */
            h = h << 4;
            str++;
        }
    }

    return h;
}

/* first bytes of UTF8 encoding for a given length in bytes */
static const unsigned char firstByteMark[5] =
{
    0x00, /* should never happen */
    0x00, /* 0xxxxxxx */
    0xC0, /* 110xxxxx */
    0xE0, /* 1110xxxx */
    0xF0 /* 11110xxx */
};

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char *parse_string(cJSON *item, const unsigned char *str, const unsigned char **ep, const cJSON_Hooks * const hooks)
{
    const unsigned char *ptr = str + 1;
    const unsigned char *end_ptr = str + 1;
    unsigned char *ptr2 = NULL;
    unsigned char *out = NULL;
    size_t len = 0;
    unsigned uc = 0;
    unsigned uc2 = 0;

    /* not a string! */
    if (*str != '\"')
    {
        *ep = str;
        goto fail;
    }

    while ((*end_ptr != '\"') && *end_ptr)
    {
        if (*end_ptr++ == '\\')
        {
            if (*end_ptr == '\0')
            {
                /* prevent buffer overflow when last input character is a backslash */
                goto fail;
            }
            /* Skip escaped quotes. */
            end_ptr++;
        }
        len++;
    }

    /* This is at most how long we need for the string, roughly. */
    out = (unsigned char*)hooks->malloc_fn(len + 1);
    if (!out)
    {
        goto fail;
    }
    item->string = (char*)out; /* assign here so out will be deleted during internal_cJSON_Delete() later */
    item->type = cJSON_String;

    ptr = str + 1;
    ptr2 = out;
    /* loop through the string literal */
    while (ptr < end_ptr)
    {
        if (*ptr != '\\')
        {
            *ptr2++ = *ptr++;
        }
        /* escape sequence */
        else
        {
            ptr++;
            switch (*ptr)
            {
                case 'b':
                    *ptr2++ = '\b';
                    break;
                case 'f':
                    *ptr2++ = '\f';
                    break;
                case 'n':
                    *ptr2++ = '\n';
                    break;
                case 'r':
                    *ptr2++ = '\r';
                    break;
                case 't':
                    *ptr2++ = '\t';
                    break;
                case '\"':
                case '\\':
                case '/':
                    *ptr2++ = *ptr;
                    break;
                case 'u':
                    /* transcode utf16 to utf8. See RFC2781 and RFC3629. */
                    uc = parse_hex4(ptr + 1); /* get the unicode char. */
                    ptr += 4;
                    if (ptr >= end_ptr)
                    {
                        /* invalid */
                        *ep = str;
                        goto fail;
                    }
                    /* check for invalid. */
                    if (((uc >= 0xDC00) && (uc <= 0xDFFF)) || (uc == 0))
                    {
                        *ep = str;
                        goto fail;
                    }

                    /* UTF16 surrogate pairs. */
                    if ((uc >= 0xD800) && (uc<=0xDBFF))
                    {
                        if ((ptr + 6) > end_ptr)
                        {
                            /* invalid */
                            *ep = str;
                            goto fail;
                        }
                        if ((ptr[1] != '\\') || (ptr[2] != 'u'))
                        {
                            /* missing second-half of surrogate. */
                            *ep = str;
                            goto fail;
                        }
                        uc2 = parse_hex4(ptr + 3);
                        ptr += 6; /* \uXXXX */
                        if ((uc2 < 0xDC00) || (uc2 > 0xDFFF))
                        {
                            /* invalid second-half of surrogate. */
                            *ep = str;
                            goto fail;
                        }
                        /* calculate unicode codepoint from the surrogate pair */
                        uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                    }

                    /* encode as UTF8
                     * takes at maximum 4 bytes to encode:
                     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
                    len = 4;
                    if (uc < 0x80)
                    {
                        /* normal ascii, encoding 0xxxxxxx */
                        len = 1;
                    }
                    else if (uc < 0x800)
                    {
                        /* two bytes, encoding 110xxxxx 10xxxxxx */
                        len = 2;
                    }
                    else if (uc < 0x10000)
                    {
                        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
                        len = 3;
                    }
                    ptr2 += len;

                    switch (len) {
                        case 4:
                            /* 10xxxxxx */
                            *--ptr2 = (unsigned char)((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 3:
                            /* 10xxxxxx */
                            *--ptr2 = (unsigned char)((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 2:
                            /* 10xxxxxx */
                            *--ptr2 = (unsigned char)((uc | 0x80) & 0xBF);
                            uc >>= 6;
                        case 1:
                            /* depending on the length in bytes this determines the
                             * encoding ofthe first UTF8 byte */
                            *--ptr2 = (unsigned char)((uc | firstByteMark[len]) & 0xFF);
                            break;
                        default:
                            *ep = str;
                            goto fail;
                    }
                    ptr2 += len;
                    break;
                default:
                    *ep = str;
                    goto fail;
            }
            ptr++;
        }
    }
    *ptr2 = '\0';
    if (*ptr == '\"')
    {
        ptr++;
    }

    return ptr;

fail:
    if (out != NULL)
    {
        hooks->free_fn(out);
    }

    return NULL;
}

/* Render the cstring provided to an escaped version that can be printed. */
static unsigned char *print_string_ptr(const unsigned char *str, printbuffer *p, const cJSON_Hooks * const hooks)
{
    const unsigned char *ptr = NULL;
    unsigned char *ptr2 = NULL;
    unsigned char *out = NULL;
    size_t len = 0;
    cjbool flag = false;
    unsigned char token = '\0';

    /* empty string */
    if (!str)
    {
        if (p)
        {
            out = ensure(p, 3, hooks);
        }
        else
        {
            out = (unsigned char*)hooks->malloc_fn(3);
        }
        if (!out)
        {
            return NULL;
        }
        strcpy((char*)out, "\"\"");

        return out;
    }

    /* set "flag" to 1 if something needs to be escaped */
    for (ptr = str; *ptr; ptr++)
    {
        flag |= (((*ptr > 0) && (*ptr < 32)) /* unprintable characters */
                || (*ptr == '\"') /* double quote */
                || (*ptr == '\\')) /* backslash */
            ? 1
            : 0;
    }
    /* no characters have to be escaped */
    if (!flag)
    {
        len = (size_t)(ptr - str);
        if (p)
        {
            out = ensure(p, len + 3, hooks);
        }
        else
        {
            out = (unsigned char*)hooks->malloc_fn(len + 3);
        }
        if (!out)
        {
            return NULL;
        }

        ptr2 = out;
        *ptr2++ = '\"';
        strcpy((char*)ptr2, (const char*)str);
        ptr2[len] = '\"';
        ptr2[len + 1] = '\0';

        return out;
    }

    ptr = str;
    /* calculate additional space that is needed for escaping */
    while ((token = *ptr))
    {
        ++len;
        if (strchr("\"\\\b\f\n\r\t", token))
        {
            len++; /* +1 for the backslash */
        }
        else if (token < 32)
        {
            len += 5; /* +5 for \uXXXX */
        }
        ptr++;
    }

    if (p)
    {
        out = ensure(p, len + 3, hooks);
    }
    else
    {
        out = (unsigned char*)hooks->malloc_fn(len + 3);
    }
    if (!out)
    {
        return NULL;
    }

    ptr2 = out;
    ptr = str;
    *ptr2++ = '\"';
    /* copy the string */
    while (*ptr)
    {
        if ((*ptr > 31) && (*ptr != '\"') && (*ptr != '\\'))
        {
            /* normal character, copy */
            *ptr2++ = *ptr++;
        }
        else
        {
            /* character needs to be escaped */
            *ptr2++ = '\\';
            switch (token = *ptr++)
            {
                case '\\':
                    *ptr2++ = '\\';
                    break;
                case '\"':
                    *ptr2++ = '\"';
                    break;
                case '\b':
                    *ptr2++ = 'b';
                    break;
                case '\f':
                    *ptr2++ = 'f';
                    break;
                case '\n':
                    *ptr2++ = 'n';
                    break;
                case '\r':
                    *ptr2++ = 'r';
                    break;
                case '\t':
                    *ptr2++ = 't';
                    break;
                default:
                    /* escape and print as unicode codepoint */
                    sprintf((char*)ptr2, "u%04x", token);
                    ptr2 += 5;
                    break;
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2++ = '\0';

    return out;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static unsigned char *print_string(const cJSON *item, printbuffer *p, const cJSON_Hooks * const hooks)
{
    return print_string_ptr((unsigned char*)item->string, p, hooks);
}

/* Predeclare these prototypes. */
static const unsigned char *parse_value(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks);
static unsigned char *print_value(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks);
static const unsigned char *parse_array(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks);
static unsigned char *print_array(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks);
static const unsigned char *parse_object(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks);
static unsigned char *print_object(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks);

/* Utility to jump whitespace and cr/lf */
static const unsigned char *skip(const unsigned char *in)
{
    while (in && *in && (*in <= 32))
    {
        in++;
    }

    return in;
}

/* Parse an object - create a new root, and populate. */
cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cjbool require_null_terminated)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    const unsigned char *end = NULL;
    /* use global error pointer if no specific one was given */
    const unsigned char **ep = return_parse_end ? (const unsigned char**)return_parse_end : &global_ep;
    cJSON *c = cJSON_New_Item(hooks);
    *ep = NULL;
    if (!c) /* memory fail */
    {
        return NULL;
    }

    end = parse_value(c, skip((const unsigned char*)value), ep, hooks);
    if (!end)
    {
        /* parse failure. ep is set. */
        internal_cJSON_Delete(c, hooks);
        return NULL;
    }

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated)
    {
        end = skip(end);
        if (*end)
        {
            internal_cJSON_Delete(c, hooks);
            *ep = end;
            return NULL;
        }
    }
    if (return_parse_end)
    {
        *return_parse_end = (const char*)end;
    }

    return c;
}

/* Default options for cJSON_Parse */
cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

/* Render a cJSON item/entity/structure to text. */
char *cJSON_Print(const cJSON *item)
{
    return (char*)print_value(item, 0, 1, 0, &global_hooks);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return (char*)print_value(item, 0, 0, 0, &global_hooks);
}

char *cJSON_PrintBuffered(const cJSON *item, size_t prebuffer, cjbool fmt)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    printbuffer p;
    p.buffer = (unsigned char*)hooks->malloc_fn(prebuffer);
    if (!p.buffer)
    {
        return NULL;
    }

    p.length = (size_t)prebuffer;
    p.offset = 0;
    p.noalloc = false;

    return (char*)print_value(item, 0, fmt, &p, hooks);
}

int cJSON_PrintPreallocated(cJSON *item, char *buf, const size_t len, const cjbool fmt)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    printbuffer p;
    p.buffer = (unsigned char*)buf;
    p.length = len;
    p.offset = 0;
    p.noalloc = true;
    return print_value(item, 0, fmt, &p, hooks) != NULL;
}

/* Parser core - when encountering text, process appropriately. */
static const unsigned char *parse_value(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks)
{
    if (!value)
    {
        /* Fail on null. */
        return NULL;
    }

    /* parse the different types of values */
    if (!strncmp((const char*)value, "null", 4))
    {
        item->type = cJSON_NULL;
        return value + 4;
    }
    if (!strncmp((const char*)value, "false", 5))
    {
        item->type = cJSON_False;
        return value + 5;
    }
    if (!strncmp((const char*)value, "true", 4))
    {
        item->type = cJSON_True;
        return value + 4;
    }
    if (*value == '\"')
    {
        return parse_string(item, value, ep, hooks);
    }
    if ((*value == '-') || ((*value >= '0') && (*value <= '9')))
    {
        return parse_number(item, value);
    }
    if (*value == '[')
    {
        return parse_array(item, value, ep, hooks);
    }
    if (*value == '{')
    {
        return parse_object(item, value, ep, hooks);
    }

    /* failure. */
    *ep = value;
    return NULL;
}

/* Render a value to text. */
static unsigned char *print_value(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    unsigned char *out = NULL;

    if (!item)
    {
        return NULL;
    }
    if (p)
    {
        switch ((item->type) & 0xFF)
        {
            case cJSON_NULL:
                out = ensure(p, 5, hooks);
                if (out)
                {
                    strcpy((char*)out, "null");
                }
                break;
            case cJSON_False:
                out = ensure(p, 6, hooks);
                if (out)
                {
                    strcpy((char*)out, "false");
                }
                break;
            case cJSON_True:
                out = ensure(p, 5, hooks);
                if (out)
                {
                    strcpy((char*)out, "true");
                }
                break;
            case cJSON_Number:
                out = print_number(item, p, hooks);
                break;
            case cJSON_Raw:
            {
                size_t raw_length = 0;
                if (item->string == NULL)
                {
                    if (!p->noalloc)
                    {
                        hooks->free_fn(p->buffer);
                    }
                    out = NULL;
                    break;
                }

                raw_length = strlen(item->string) + sizeof('\0');
                out = ensure(p, raw_length, hooks);
                if (out)
                {
                    memcpy(out, item->string, raw_length);
                }
                break;
            }
            case cJSON_String:
                out = print_string(item, p, hooks);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, p, hooks);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, p, hooks);
                break;
            default:
                out = NULL;
                break;
        }
    }
    else
    {
        switch ((item->type) & 0xFF)
        {
            case cJSON_NULL:
                out = cJSON_strdup((const unsigned char*)"null", hooks);
                break;
            case cJSON_False:
                out = cJSON_strdup((const unsigned char*)"false", hooks);
                break;
            case cJSON_True:
                out = cJSON_strdup((const unsigned char*)"true", hooks);
                break;
            case cJSON_Number:
                out = print_number(item, 0, hooks);
                break;
            case cJSON_Raw:
                out = cJSON_strdup((unsigned char*)item->string, hooks);
                break;
            case cJSON_String:
                out = print_string(item, 0, hooks);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, 0, hooks);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, 0, hooks);
                break;
            default:
                out = NULL;
                break;
        }
    }

    return out;
}

/* Build an array from input text. */
static const unsigned char *parse_array(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks)
{
    cJSON *child = NULL;
    if (*value != '[')
    {
        /* not an array! */
        *ep = value;
        goto fail;
    }

    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']')
    {
        /* empty array. */
        return value + 1;
    }

    item->child = child = cJSON_New_Item(hooks);
    if (!item->child)
    {
        /* memory fail */
        goto fail;
    }
    /* skip any spacing, get the value. */
    value = skip(parse_value(child, skip(value), ep, hooks));
    if (!value)
    {
        goto fail;
    }

    /* loop through the comma separated array elements */
    while (*value == ',')
    {
        cJSON *new_item = NULL;
        if (!(new_item = cJSON_New_Item(hooks)))
        {
            /* memory fail */
            goto fail;
        }
        /* add new item to end of the linked list */
        child->next = new_item;
        child = new_item;

        /* go to the next comma */
        value = skip(parse_value(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            /* memory fail */
            goto fail;
        }
    }

    if (*value == ']')
    {
        /* end of array */
        return value + 1;
    }

    /* malformed. */
    *ep = value;

fail:
    if (item->child != NULL)
    {
        cJSON_Delete(item->child);
        item->child = NULL;
    }

    return NULL;
}

/* Render an array to text */
static unsigned char *print_array(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    unsigned char **entries;
    unsigned char *out = NULL;
    unsigned char *ptr = NULL;
    unsigned char *ret = NULL;
    size_t len = 5;
    cJSON *child = item->child;
    size_t numentries = 0;
    size_t i = 0;
    cjbool fail = false;
    size_t tmplen = 0;

    /* How many entries in the array? */
    while (child)
    {
        numentries++;
        child = child->next;
    }

    /* Explicitly handle numentries == 0 */
    if (!numentries)
    {
        if (p)
        {
            out = ensure(p, 3, hooks);
        }
        else
        {
            out = (unsigned char*)hooks->malloc_fn(3);
        }
        if (out)
        {
            strcpy((char*)out, "[]");
        }

        return out;
    }

    if (p)
    {
        /* Compose the output array. */
        /* opening square bracket */
        i = p->offset;
        ptr = ensure(p, 1, hooks);
        if (!ptr)
        {
            return NULL;
        }
        *ptr = '[';
        p->offset++;

        child = item->child;
        while (child && !fail)
        {
            if (!print_value(child, depth + 1, fmt, p, hooks))
            {
                return NULL;
            }
            p->offset = update(p);
            if (child->next)
            {
                len = fmt ? 2 : 1;
                ptr = ensure(p, len + 1, hooks);
                if (!ptr)
                {
                    return NULL;
                }
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
                p->offset += len;
            }
            child = child->next;
        }
        ptr = ensure(p, 2, hooks);
        if (!ptr)
        {
            return NULL;
        }
        *ptr++ = ']';
        *ptr = '\0';
        out = (p->buffer) + i;
    }
    else
    {
        /* Allocate an array to hold the pointers to all printed values */
        entries = (unsigned char**)hooks->malloc_fn(numentries * sizeof(unsigned char*));
        if (!entries)
        {
            return NULL;
        }
        memset(entries, '\0', numentries * sizeof(unsigned char*));

        /* Retrieve all the results: */
        child = item->child;
        while (child && !fail)
        {
            ret = print_value(child, depth + 1, fmt, 0, hooks);
            entries[i++] = ret;
            if (ret)
            {
                len += strlen((char*)ret) + 2 + (fmt ? 1 : 0);
            }
            else
            {
                fail = true;
            }
            child = child->next;
        }

        /* If we didn't fail, try to malloc the output string */
        if (!fail)
        {
            out = (unsigned char*)hooks->malloc_fn(len);
        }
        /* If that fails, we fail. */
        if (!out)
        {
            fail = true;
        }

        /* Handle failure. */
        if (fail)
        {
            /* free all the entries in the array */
            for (i = 0; i < numentries; i++)
            {
                if (entries[i])
                {
                    hooks->free_fn(entries[i]);
                }
            }
            hooks->free_fn(entries);
            return NULL;
        }

        /* Compose the output array. */
        *out='[';
        ptr = out + 1;
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            tmplen = strlen((char*)entries[i]);
            memcpy(ptr, entries[i], tmplen);
            ptr += tmplen;
            if (i != (numentries - 1))
            {
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
            }
            hooks->free_fn(entries[i]);
        }
        hooks->free_fn(entries);
        *ptr++ = ']';
        *ptr++ = '\0';
    }

    return out;
}

/* Build an object from the text. */
static const unsigned char *parse_object(cJSON *item, const unsigned char *value, const unsigned char **ep, const cJSON_Hooks * const hooks)
{
    cJSON *child = NULL;
    if (*value != '{')
    {
        /* not an object! */
        *ep = value;
        goto fail;
    }

    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}')
    {
        /* empty object. */
        return value + 1;
    }

    child = cJSON_New_Item(hooks);
    item->child = child;
    if (!item->child)
    {
        goto fail;
    }
    /* parse first key */
    value = skip(parse_string(child, skip(value), ep, hooks));
    if (!value)
    {
        goto fail;
    }
    /* use parsed string as key, not value */
    child->name = child->string;
    child->string = NULL;

    if (*value != ':')
    {
        /* invalid object. */
        *ep = value;
        goto fail;
    }
    /* skip any spacing, get the value. */
    value = skip(parse_value(child, skip(value + 1), ep, hooks));
    if (!value)
    {
        goto fail;
    }

    while (*value == ',')
    {
        cJSON *new_item = NULL;
        if (!(new_item = cJSON_New_Item(hooks)))
        {
            /* memory fail */
            goto fail;
        }
        /* add to linked list */
        child->next = new_item;

        child = new_item;
        value = skip(parse_string(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            goto fail;
        }

        /* use parsed string as key, not value */
        child->name = child->string;
        child->string = NULL;

        if (*value != ':')
        {
            /* invalid object. */
            *ep = value;
            goto fail;
        }
        /* skip any spacing, get the value. */
        value = skip(parse_value(child, skip(value + 1), ep, hooks));
        if (!value)
        {
            goto fail;
        }
    }
    /* end of object */
    if (*value == '}')
    {
        return value + 1;
    }

    /* malformed */
    *ep = value;

fail:
    if (item->child != NULL)
    {
        cJSON_Delete(child);
        item->child = NULL;
    }

    return NULL;
}

/* Render an object to text. */
static unsigned char *print_object(const cJSON *item, size_t depth, cjbool fmt, printbuffer *p, const cJSON_Hooks * const hooks)
{
    unsigned char **entries = NULL;
    unsigned char **names = NULL;
    unsigned char *out = NULL;
    unsigned char *ptr = NULL;
    unsigned char *ret = NULL;
    unsigned char *str = NULL;
    size_t len = 7;
    size_t i = 0;
    size_t j = 0;
    cJSON *child = item->child;
    size_t numentries = 0;
    cjbool fail = false;
    size_t tmplen = 0;

    /* Count the number of entries. */
    while (child)
    {
        numentries++;
        child = child->next;
    }

    /* Explicitly handle empty object case */
    if (!numentries)
    {
        if (p)
        {
            out = ensure(p, fmt ? depth + 4 : 3, hooks);
        }
        else
        {
            out = (unsigned char*)hooks->malloc_fn(fmt ? depth + 4 : 3);
        }
        if (!out)
        {
            return NULL;
        }
        ptr = out;
        *ptr++ = '{';
        if (fmt) {
            *ptr++ = '\n';
            for (i = 0; i < depth; i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';

        return out;
    }

    if (p)
    {
        /* Compose the output: */
        i = p->offset;
        len = fmt ? 2 : 1; /* fmt: {\n */
        ptr = ensure(p, len + 1, hooks);
        if (!ptr)
        {
            return NULL;
        }

        *ptr++ = '{';
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        p->offset += len;

        child = item->child;
        depth++;
        while (child)
        {
            if (fmt)
            {
                ptr = ensure(p, depth, hooks);
                if (!ptr)
                {
                    return NULL;
                }
                for (j = 0; j < depth; j++)
                {
                    *ptr++ = '\t';
                }
                p->offset += depth;
            }

            /* print key */
            if (!print_string_ptr((unsigned char*)child->name, p, hooks))
            {
                return NULL;
            }
            p->offset = update(p);

            len = fmt ? 2 : 1;
            ptr = ensure(p, len, hooks);
            if (!ptr)
            {
                return NULL;
            }
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            p->offset+=len;

            /* print value */
            if (!print_value(child, depth, fmt, p, hooks))
            {
                return NULL;
            };
            p->offset = update(p);

            /* print comma if not last */
            len = (size_t) (fmt ? 1 : 0) + (child->next ? 1 : 0);
            ptr = ensure(p, len + 1, hooks);
            if (!ptr)
            {
                return NULL;
            }
            if (child->next)
            {
                *ptr++ = ',';
            }

            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            p->offset += len;

            child = child->next;
        }

        ptr = ensure(p, fmt ? (depth + 1) : 2, hooks);
        if (!ptr)
        {
            return NULL;
        }
        if (fmt)
        {
            for (i = 0; i < (depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr = '\0';
        out = (p->buffer) + i;
    }
    else
    {
        /* Allocate space for the names and the objects */
        entries = (unsigned char**)hooks->malloc_fn(numentries * sizeof(unsigned char*));
        if (!entries)
        {
            return NULL;
        }
        names = (unsigned char**)hooks->malloc_fn(numentries * sizeof(unsigned char*));
        if (!names)
        {
            hooks->free_fn(entries);
            return NULL;
        }
        memset(entries, '\0', sizeof(unsigned char*) * numentries);
        memset(names, '\0', sizeof(unsigned char*) * numentries);

        /* Collect all the results into our arrays: */
        child = item->child;
        depth++;
        if (fmt)
        {
            len += depth;
        }
        while (child && !fail)
        {
            names[i] = str = print_string_ptr((unsigned char*)child->name, 0, hooks); /* print key */
            entries[i++] = ret = print_value(child, depth, fmt, 0, hooks);
            if (str && ret)
            {
                len += strlen((char*)ret) + strlen((char*)str) + 2 + (fmt ? 2 + depth : 0);
            }
            else
            {
                fail = true;
            }
            child = child->next;
        }

        /* Try to allocate the output string */
        if (!fail)
        {
            out = (unsigned char*)hooks->malloc_fn(len);
        }
        if (!out)
        {
            fail = true;
        }

        /* Handle failure */
        if (fail)
        {
            /* free all the printed keys and values */
            for (i = 0; i < numentries; i++)
            {
                if (names[i])
                {
                    hooks->free_fn(names[i]);
                }
                if (entries[i])
                {
                    hooks->free_fn(entries[i]);
                }
            }
            hooks->free_fn(names);
            hooks->free_fn(entries);
            return NULL;
        }

        /* Compose the output: */
        *out = '{';
        ptr = out + 1;
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            if (fmt)
            {
                for (j = 0; j < depth; j++)
                {
                    *ptr++='\t';
                }
            }
            tmplen = strlen((char*)names[i]);
            memcpy(ptr, names[i], tmplen);
            ptr += tmplen;
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            strcpy((char*)ptr, (char*)entries[i]);
            ptr += strlen((char*)entries[i]);
            if (i != (numentries - 1))
            {
                *ptr++ = ',';
            }
            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            hooks->free_fn(names[i]);
            hooks->free_fn(entries[i]);
        }

        hooks->free_fn(names);
        hooks->free_fn(entries);
        if (fmt)
        {
            for (i = 0; i < (depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';
    }

    return out;
}

/* Get Array size/item / object item. */
size_t cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array->child;
    size_t i = 0;
    while(c)
    {
        i++;
        c = c->next;
    }

    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, size_t item)
{
    cJSON *c = array ? array->child : NULL;
    while (c && item > 0)
    {
        item--;
        c = c->next;
    }

    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *name)
{
    cJSON *c = object ? object->child : NULL;
    while (c && strcmp(c->name, name))
    {
        c = c->next;
    }
    return c;
}

cjbool cJSON_HasObjectItem(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItem(object, name) ? 1 : 0;
}

/* Utility for array list handling. */
static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item;
}

/* Utility for handling references. */
static cJSON *create_reference(const cJSON *item, const cJSON_Hooks * const hooks)
{
    cJSON *ref = cJSON_New_Item(hooks);
    if (!ref)
    {
        return NULL;
    }
    memcpy(ref, item, sizeof(cJSON));
    ref->name = NULL;
    ref->is_reference = true;
    ref->next = NULL;
    return ref;
}

/* Add item to array/object. */
void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *child = NULL;

    if ((item == NULL) || (array == NULL))
    {
        return;
    }

    child = array->child;

    if (child == NULL)
    {
        /* list is empty, start new one */
        array->child = item;
    }
    else
    {
        /* append to the end */
        while (child->next)
        {
            child = child->next;
        }
        suffix_object(child, item);
    }
}

void   cJSON_AddItemToObject(cJSON *object, const char *name, cJSON *item)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    if (!item)
    {
        return;
    }

    /* free old key and set new one */
    if (item->name)
    {
        hooks->free_fn(item->name);
    }
    item->name = (char*)cJSON_strdup((const unsigned char*)name, hooks);

    cJSON_AddItemToArray(object,item);
}

/* Add an item to an object with constant string as key */
void   cJSON_AddItemToObjectCS(cJSON *object, const char *name, cJSON *item)
{
    const cJSON_Hooks * const hooks = &global_hooks;
    if (!item)
    {
        return;
    }
    if (!item->string_is_const && item->name)
    {
        hooks->free_fn(item->name);
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    item->name = (char*)name;
#pragma GCC diagnostic pop
    item->string_is_const = true;
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)
{
    cJSON_AddItemToArray(array, create_reference(item, &global_hooks));
}

void cJSON_AddItemReferenceToObject(cJSON *object, const char *name, cJSON *item)
{
    cJSON_AddItemToObject(object, name, create_reference(item, &global_hooks));
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, size_t which)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        /* item doesn't exist */
        return NULL;
    }
    if (c == array->child)
    {
        /* first element */
        array->child = c->next;
    }
    else
    {
        /* not the first element */
        previous->next = c->next;
    }

    /* make sure the detached item doesn't point anywhere anymore */
    c->next = NULL;

    return c;
}

static void internal_cJSON_DeleteItemFromArray(cJSON *array, size_t which, const cJSON_Hooks * const hooks)
{
    internal_cJSON_Delete(cJSON_DetachItemFromArray(array, which), hooks);
}
void cJSON_DeleteItemFromArray(cJSON * array, size_t which)
{
    internal_cJSON_DeleteItemFromArray(array, which, &global_hooks);
}

cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *name)
{
    size_t i = 0;
    cJSON *c = object->child;
    while (c && strcmp(c->name, name))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        return cJSON_DetachItemFromArray(object, i);
    }

    return NULL;
}

static void internal_cJSON_DeleteItemFromObject(cJSON *object, const char *name, const cJSON_Hooks * const hooks)
{
    internal_cJSON_Delete(cJSON_DetachItemFromObject(object, name), hooks);
}
void cJSON_DeleteItemFromObject(cJSON *object, const char *name)
{
    internal_cJSON_DeleteItemFromObject(object, name, &global_hooks);
}

/* Replace array/object items with new ones. */
void cJSON_InsertItemInArray(cJSON *array, size_t which, cJSON *newitem)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        cJSON_AddItemToArray(array, newitem);
        return;
    }
    newitem->next = c;
    if (c == array->child)
    {
        /* first element */
        array->child = newitem;
        return;
    }

    /* not first element */
    previous->next = newitem;
}

static void internal_cJSON_ReplaceItemInArray(cJSON *array, size_t which, cJSON *newitem, const cJSON_Hooks * const hooks)
{
    cJSON *c = array->child;
    cJSON *previous = NULL;
    while (c && (which > 0))
    {
        previous = c;
        c = c->next;
        which--;
    }
    if (!c)
    {
        return;
    }
    newitem->next = c->next;
    if (c == array->child)
    {
        /* first element */
        array->child = newitem;
    }
    else
    {
        /* not first element */
        previous->next = newitem;
    }

    /* make sure the replaced item doesn't point anywhere anymore */
    c->next = NULL;
    internal_cJSON_Delete(c, hooks);
}
void cJSON_ReplaceItemInArray(cJSON *array, size_t which, cJSON *newitem)
{
    internal_cJSON_ReplaceItemInArray(array, which, newitem, &global_hooks);
}

static void internal_cJSON_ReplaceItemInObject(cJSON *object, const char *name, cJSON *newitem, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *c = object->child;
    while(c && strcmp(c->name, name))
    {
        i++;
        c = c->next;
    }
    if(c)
    {
        /* free the old name if not const */
        if (!newitem->string_is_const && newitem->name)
        {
             hooks->free_fn(newitem->name);
        }

        newitem->name = (char*)cJSON_strdup((const unsigned char*)name, hooks);
        internal_cJSON_ReplaceItemInArray(object, i, newitem, hooks);
    }
}
void cJSON_ReplaceItemInObject(cJSON *object, const char *name, cJSON *newitem)
{
    internal_cJSON_ReplaceItemInObject(object, name, newitem, &global_hooks);
}

/* Create basic types: */
static cJSON *internal_cJSON_CreateNull(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_NULL;
    }

    return item;
}
cJSON *cJSON_CreateNull(void)
{
    return internal_cJSON_CreateNull(&global_hooks);
}

static cJSON *internal_cJSON_CreateTrue(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_True;
    }

    return item;
}
cJSON *cJSON_CreateTrue(void)
{
    return internal_cJSON_CreateTrue(&global_hooks);
}

static cJSON *internal_cJSON_CreateFalse(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_False;
    }

    return item;
}
cJSON *cJSON_CreateFalse(void)
{
    return internal_cJSON_CreateFalse(&global_hooks);
}

static cJSON *internal_cJSON_CreateBool(cjbool b, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = b ? cJSON_True : cJSON_False;
    }

    return item;
}
cJSON *cJSON_CreateBool(int b)
{
    return internal_cJSON_CreateBool(b, &global_hooks);
}

static cJSON *internal_cJSON_CreateNumber(double num, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_Number;
        item->number = num;
    }

    return item;
}
cJSON *cJSON_CreateNumber(double num)
{
    return internal_cJSON_CreateNumber(num, &global_hooks);
}

static cJSON *internal_cJSON_CreateString(const char *string, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_String;
        item->string = (char*)cJSON_strdup((const unsigned char*)string, hooks);
        if(!item->string)
        {
            internal_cJSON_Delete(item, hooks);
            return NULL;
        }
    }

    return item;
}
cJSON *cJSON_CreateString(const char *string)
{
    return internal_cJSON_CreateString(string, &global_hooks);
}

static cJSON *internal_cJSON_CreateRaw(const char *raw, const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type = cJSON_Raw;
        item->string = (char*)cJSON_strdup((const unsigned char*)raw, hooks);
        if(!item->string)
        {
            internal_cJSON_Delete(item, hooks);
            return NULL;
        }
    }

    return item;
}
extern cJSON *cJSON_CreateRaw(const char *raw)
{
    return internal_cJSON_CreateRaw(raw, &global_hooks);
}

static cJSON *internal_cJSON_CreateArray(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if(item)
    {
        item->type=cJSON_Array;
    }

    return item;
}
cJSON *cJSON_CreateArray(void)
{
    return internal_cJSON_CreateArray(&global_hooks);
}

static cJSON *internal_cJSON_CreateObject(const cJSON_Hooks * const hooks)
{
    cJSON *item = cJSON_New_Item(hooks);
    if (item)
    {
        item->type = cJSON_Object;
    }

    return item;
}
cJSON *cJSON_CreateObject(void)
{
    return internal_cJSON_CreateObject(&global_hooks);
}

/* Create Arrays: */
static cJSON *internal_cJSON_CreateIntArray(const int *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    a = internal_cJSON_CreateArray(hooks);
    for(i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if (!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateIntArray(const int *numbers, size_t count)
{
    return internal_cJSON_CreateIntArray(numbers, count, &global_hooks);
}

static cJSON *internal_cJSON_CreateFloatArray(const float *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    a = internal_cJSON_CreateArray(hooks);

    for(i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateFloatArray(const float *numbers, size_t count)
{
    return internal_cJSON_CreateFloatArray(numbers, count, &global_hooks);
}

static cJSON *internal_cJSON_CreateDoubleArray(const double *numbers, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    a = internal_cJSON_CreateArray(hooks);

    for(i = 0;a && (i < count); i++)
    {
        n = internal_cJSON_CreateNumber(numbers[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateDoubleArray(const double *numbers, size_t count)
{
    return internal_cJSON_CreateDoubleArray(numbers, count, &global_hooks);
}

static cJSON *internal_cJSON_CreateStringArray(const char **strings, size_t count, const cJSON_Hooks * const hooks)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    a = internal_cJSON_CreateArray(hooks);

    for (i = 0; a && (i < count); i++)
    {
        n = internal_cJSON_CreateString(strings[i], hooks);
        if(!n)
        {
            internal_cJSON_Delete(a, hooks);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p,n);
        }
        p = n;
    }

    return a;
}
cJSON *cJSON_CreateStringArray(const char **strings, size_t count)
{
    return internal_cJSON_CreateStringArray(strings, count, &global_hooks);
}

/* Duplication */
static cJSON *internal_cJSON_Duplicate(const cJSON *item, cjbool recurse, const cJSON_Hooks * const hooks)
{
    cJSON *newitem = NULL;
    cJSON *child = NULL;
    cJSON *next = NULL;
    cJSON *newchild = NULL;

    /* Bail on bad ptr */
    if (!item)
    {
        goto fail;
    }
    /* Create new item */
    newitem = cJSON_New_Item(hooks);
    if (!newitem)
    {
        goto fail;
    }
    /* Copy over all vars */
    newitem->type = item->type;
    newitem->is_reference = false;
    newitem->number = item->number;
    newitem->string_is_const = false;
    if (item->string)
    {
        newitem->string = (char*)cJSON_strdup((unsigned char*)item->string, hooks);
        if (!newitem->string)
        {
            goto fail;
        }
    }
    if (item->name)
    {
        newitem->name = (item->string_is_const) ? item->name : (char*)cJSON_strdup((unsigned char*)item->name, hooks);
        if (!newitem->name)
        {
            goto fail;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse)
    {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    child = item->child;
    while (child != NULL)
    {
        newchild = internal_cJSON_Duplicate(child, 1, hooks); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild)
        {
            goto fail;
        }
        if (next != NULL)
        {
            /* If newitem->child already set, then add newchild and move on */
            next->next = newchild;
            next = newchild;
        }
        else
        {
            /* Set newitem->child and move to it */
            newitem->child = newchild;
            next = newchild;
        }
        child = child->next;
    }

    return newitem;

fail:
    if (newitem != NULL)
    {
        cJSON_Delete(newitem);
    }

    return NULL;
}
cJSON *cJSON_Duplicate(const cJSON *item, int recurse)
{
    return internal_cJSON_Duplicate(item, recurse, &global_hooks);
}

void cJSON_Minify(char *json)
{
    unsigned char *into = (unsigned char*)json;
    while (*json)
    {
        if (*json == ' ')
        {
            json++;
        }
        else if (*json == '\t')
        {
            /* Whitespace characters. */
            json++;
        }
        else if (*json == '\r')
        {
            json++;
        }
        else if (*json=='\n')
        {
            json++;
        }
        else if ((*json == '/') && (json[1] == '/'))
        {
            /* double-slash comments, to end of line. */
            while (*json && (*json != '\n'))
            {
                json++;
            }
        }
        else if ((*json == '/') && (json[1] == '*'))
        {
            /* multiline comments. */
            while (*json && !((*json == '*') && (json[1] == '/')))
            {
                json++;
            }
            json += 2;
        }
        else if (*json == '\"')
        {
            /* string literals, which are \" sensitive. */
            *into++ = (unsigned char)*json++;
            while (*json && (*json != '\"'))
            {
                if (*json == '\\')
                {
                    *into++ = (unsigned char)*json++;
                }
                *into++ = (unsigned char)*json++;
            }
            *into++ = (unsigned char)*json++;
        }
        else
        {
            /* All other characters. */
            *into++ = (unsigned char)*json++;
        }
    }

    /* and null-terminate. */
    *into = '\0';
}
