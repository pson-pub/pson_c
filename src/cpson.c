#include "cpson.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <float.h>

/* define isnan and isinf for ANSI C, if in C99 or above, isnan and isinf has been defined in math.h */
#ifndef isnan
#define isnan(d) (d != d)
#endif
#ifndef isinf
#define isinf(d) (isnan((d - d)) && !isnan(d))
#endif

#define internal_malloc malloc
#define internal_free free
#define internal_realloc realloc

// dynamic-array
typedef struct {
    int cap;
    int size;
    cPSON** ptr;
} PtrList;

PtrList* cPSON_NewList() {
    PtrList* list = (PtrList*)malloc(sizeof(PtrList));
    // list->ptr = malloc(sizeof(void*)*16);
    // list->cap = 16;
    list->ptr = NULL;
    list->cap = 0;
    list->size = 0;
    return list;
}
void cPSON_ReleaseList(PtrList* const list) {
    if (list->ptr) {
        free(list->ptr);
    }
    free(list);
}
int cPSON_PtrList_add(PtrList* list, cPSON* item) {
    if (list->size == list->cap) {
        int cap = list->cap + 16;
        void* ptr = malloc(sizeof(cPSON*) * cap);
        if (list->ptr) {
            memcpy(ptr, list->ptr, sizeof(cPSON*) * list->cap);
            free(list->ptr);
        }
        list->ptr = ptr;
        list->cap = cap;
    }
    // cPSON **ptr = list->ptr+list->size;
    // *ptr = item;
    list->ptr[list->size] = item;
    list->size++;
    return list->size;
}

#define CPSON_CDECL
typedef struct internal_hooks {
    void*(CPSON_CDECL* allocate)(size_t size);
    void(CPSON_CDECL* deallocate)(void* pointer);
    void*(CPSON_CDECL* reallocate)(void* pointer, size_t size);
} internal_hooks;

static internal_hooks global_hooks = {internal_malloc, internal_free, internal_realloc};
// // debug hooks
// static int mem_count = 0;
// void* debug_allocate(size_t size){
//     mem_count++;
//     return malloc(size);
// }
// void debug_deallocate(void* pointer){
//     free(pointer);
//     mem_count--;
//     printf("debug_deallocate %d\n",mem_count);
// }
// static internal_hooks global_hooks = {debug_allocate, debug_deallocate, internal_realloc};

typedef struct {
    const unsigned char* content;
    size_t length;
    size_t offset;
    size_t depth; /* How deeply nested (in arrays/objects) is the input at the current offset. */
    internal_hooks hooks;
} parse_buffer;

#define can_read(buffer, size) ((buffer != NULL) && (((buffer)->offset + size) <= (buffer)->length))
#define has_char(buffer) ((buffer)->offset < (buffer)->length)
#define has_more_char(buffer, index) ((buffer != NULL) && (((buffer)->offset + index) < (buffer)->length))
#define buffer_at_offset(buffer) ((buffer)->content + (buffer)->offset)

// #define char_at_offset(buffer) (char)((buffer)->content[(buffer)->offset])
#define char_at_offset(buffer) (char)((buffer)->content[(buffer)->offset])

static parse_buffer* skip_utf8_bom(parse_buffer* const buffer) {
    if (buffer == NULL || buffer->content == NULL || buffer->offset != 0) {
        return NULL;
    }
    if (buffer->length > 4) {
        if (strncmp((const char*)(buffer->content), "\xEF\xBB\xBF", 3) == 0) {
            buffer->offset = 3;
        }
    }
    return buffer;
}
static parse_buffer* skip_comment(parse_buffer* const buffer) {
    if ((buffer == NULL) || (buffer->content == NULL)) {
        return NULL;
    }
    if (char_at_offset(buffer) == '#') {
        // read to \n
        buffer->offset++;
        while (has_char(buffer)) {
            buffer->offset++;
            if (char_at_offset(buffer) == '\n') {
                buffer->offset++;
                break;
            }
        }
    }
    return buffer;
}
static parse_buffer* skip_whitespace(parse_buffer* const buffer) {
    if ((buffer == NULL) || (buffer->content == NULL)) {
        return NULL;
    }
    // escape space \r \n
    while (has_char(buffer) && (buffer_at_offset(buffer)[0] <= 32)) {
        buffer->offset++;
    }
    return buffer;
}

static parse_buffer* skip_whitespace_comment(parse_buffer* const buffer) {
    do {
        int offset = buffer->offset;
        skip_whitespace(buffer);
        skip_comment(buffer);
        if (offset == buffer->offset) {
            break;
        }
    } while (1);
    return buffer;
}

/* parse 4 digit hexadecimal number */
static unsigned parse_hex4(const unsigned char* const input) {
    unsigned int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++) {
        /* parse digit */
        if ((input[i] >= '0') && (input[i] <= '9')) {
            h += (unsigned int)input[i] - '0';
        } else if ((input[i] >= 'A') && (input[i] <= 'F')) {
            h += (unsigned int)10 + input[i] - 'A';
        } else if ((input[i] >= 'a') && (input[i] <= 'f')) {
            h += (unsigned int)10 + input[i] - 'a';
        } else /* invalid */
        {
            return 0;
        }

        if (i < 3) {
            /* shift left to make place for the next nibble */
            h = h << 4;
        }
    }

    return h;
}
static int utf16_literal_to_utf8(const unsigned char* const input_pointer, const unsigned char* const input_end,
                                 unsigned char** output_pointer) {
    long unsigned int codepoint = 0;
    unsigned int first_code = 0;
    const unsigned char* first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6) {
        /* input ends unexpectedly */
        return 0;
    }

    /* get the first utf16 sequence */
    first_code = parse_hex4(first_sequence + 2);

    /* check that the code is valid */
    if (first_code == 0 || ((first_code >= 0xDC00) && (first_code <= 0xDFFF))) {
        return 0;
    }

    /* UTF16 surrogate pair */
    if ((first_code >= 0xD800) && (first_code <= 0xDBFF)) {
        const unsigned char* second_sequence = first_sequence + 6;
        unsigned int second_code = 0;
        sequence_length = 12; /* \uXXXX\uXXXX */

        if ((input_end - second_sequence) < 6) {
            /* input ends unexpectedly */
            return 0;
        }

        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u')) {
            /* missing second half of the surrogate pair */
            return 0;
        }

        /* get the second utf16 sequence */
        second_code = parse_hex4(second_sequence + 2);
        /* check that the code is valid */
        if ((second_code < 0xDC00) || (second_code > 0xDFFF)) {
            /* invalid second half of the surrogate pair */
            return 0;
        }

        /* calculate the unicode codepoint from the surrogate pair */
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    } else {
        sequence_length = 6; /* \uXXXX */
        codepoint = first_code;
    }

    /* encode as UTF-8
     * takes at maximum 4 bytes to encode:
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint < 0x80) {
        /* normal ascii, encoding 0xxxxxxx */
        utf8_length = 1;
    } else if (codepoint < 0x800) {
        /* two bytes, encoding 110xxxxx 10xxxxxx */
        utf8_length = 2;
        first_byte_mark = 0xC0; /* 11000000 */
    } else if (codepoint < 0x10000) {
        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
        utf8_length = 3;
        first_byte_mark = 0xE0; /* 11100000 */
    } else if (codepoint <= 0x10FFFF) {
        /* four bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8_length = 4;
        first_byte_mark = 0xF0; /* 11110000 */
    } else {
        /* invalid unicode codepoint */
        return 0;
    }

    /* encode as utf8 */
    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--) {
        /* 10xxxxxx */
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    /* encode first byte */
    if (utf8_length > 1) {
        (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    } else {
        (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;

    return sequence_length;
}
cPSON* cPSON_New_Item(const internal_hooks* const hooks) {
    cPSON* item = (cPSON*)hooks->allocate(sizeof(cPSON));
    if (item) {
        memset(item, 0, sizeof(cPSON));
    }
    return item;
}
static cPSON_bool is_split_char(char ch) {
    switch (ch) {
        case ' ':
        case ':':
        case ',':
        case '{':
        case '[':
            return True;
        default:
            return False;
    }
    return False;
}
/// @brief read a key word
/// @param buffer source data
/// @return key word string copied from source data.
static char* read_until_split(parse_buffer* const buffer) {
    // split symbol: space : { ] ,
    // ptr, \0
    char* ptr = (char*)(buffer->content + buffer->offset);
    int len = 0;
    do {
        if (is_split_char(char_at_offset(buffer))) {
            char* str = (char*)global_hooks.allocate(len + 1);
            strncpy(str, ptr, len);
            str[len] = '\0';
            return str;
        }
        len++;
        buffer->offset++;

    } while (1);
    return NULL;
}

static cPSON_bool parse_value(cPSON* const item, parse_buffer* const buffer, PtrList* const templates,
                              const cPSON* template);

static cPSON_bool parse_number(cPSON* const item, parse_buffer* const buffer) {
    // [-][0-9][.][0-9]
    double number = 0;
    char* after_end = NULL;
    char* ptr = buffer_at_offset(buffer);
    number = strtod(ptr, &after_end);
    if (after_end == ptr) {
        printf("number parse failed\n");
        return False;
    }
    item->valuedouble = number;

    item->type = cPSON_Number;
    buffer->offset += (size_t)(after_end - ptr);
    return True;
}
// process escape characters
static cPSON_bool str_filte(char* dst, const char* src, int len) {
    const char* src_end = src + len;
    while (src < src_end) {
        if (*src != '\\') {
            *dst++ = *src++;
        } else {
            switch (src[1]) {
                case 'b':
                    *dst++ = '\b';
                    break;
                case 'f':
                    *dst++ = '\f';
                    break;
                case 'r':
                    *dst++ = '\r';
                    break;
                case 'n':
                    *dst++ = '\n';
                    break;
                case 't':
                    *dst++ = '\t';
                    break;
                case '\'':
                case '"':
                case '\\':
                    *dst++ = src[1];
                    break;
                case 'u': {
                    // utf-16 literal: 4 hex digits
                    int sequence_length = utf16_literal_to_utf8(src, src_end, &dst);
                    if (sequence_length == 0) {
                        printf("failed to convert UTF16-literal to UTF-8 %d\n", sequence_length);
                        *dst++ = '\0';
                        return False;
                    }
                    src += (sequence_length - 2);
                    break;
                }
                default:
                    printf("unsupported escape code \\%c\n",src[1]);
                    return False;
            }
            src += 2;
        }
    }
    *dst++ = '\0';
    return True;
}
// parse string value, and convert escaped-code.
static cPSON_bool parse_string(cPSON* const item, parse_buffer* const buffer) {
    // 'xxx'  or "xxx"
    char quto = char_at_offset(buffer);
    buffer->offset++;
    char last_char = 0;
    char* ptr = buffer_at_offset(buffer);
    int len = 0;
    do {
        char ch = char_at_offset(buffer);
        if (ch == quto) {
            if (last_char != '\\') {
                buffer->offset++;
                last_char = ch;
                break;
            }
        }
        len++;
        last_char = ch;
        buffer->offset++;
    } while (has_char(buffer));
    if (last_char != quto) {
        return False;
    }
    char* str = (char*)global_hooks.allocate(len + 1);
    // strncpy(str, ptr, len);
    if (str_filte(str, ptr, len) == False) {
        return False;
    }

    str[len] = '\0';
    item->valuestring = str;
    item->type = cPSON_String;
    return True;
}
// parse array values
static cPSON_bool parse_array(cPSON* const parent, parse_buffer* const buffer, PtrList* const templates,
                              const cPSON* used_template) {
    // [ number | string | bool | object | array ]
    if (char_at_offset(buffer) != '[') {
        return False;
    }
    buffer->offset++;
    cPSON* head = NULL;
    cPSON* cur = NULL;
    cPSON_bool got_error = False;
    do {
        cPSON* item = NULL;
        skip_whitespace_comment(buffer);
        
        if (char_at_offset(buffer) == ',') {
            buffer->offset++;
            continue;
        }
        if (char_at_offset(buffer) == ']') {
            buffer->offset++;
            break;
        }
        item = cPSON_New_Item(&(buffer->hooks));
        if (used_template) {
            item->template_name = used_template->name;
        }
        if (!parse_value(item, buffer, templates, used_template)) {
            got_error = True;
            break;
        }

        if (cur) {
            cur->next = item;
            item->prev = cur;
        }
        if (head == NULL) {
            head = item;
        }
        cur = item;
    } while (1);
    if (got_error) {
        if (head) {
            cPSON_Release(head);
        }
        return False;
    }
    if (head != NULL) {
        head->prev = cur;
    }
    parent->child = head;
    parent->type = cPSON_Array;
    return True;
}
// find template in key. x<x>
// return template name start offset.
static int str_find_template_name(const char* str, int* size) {
    int len = strlen(str);
    int start = -1;
    if (len > 3 && str[len - 1] == '>') {
        for (int i = 1; i < len; i++) {
            if (str[i] == '<') {
                start = i + 1;
            }
        }
        if (start > 0) {  // [start, len-1)
            *size = len - 1 - start;
            return start;
        }
    }

    return start;
}
static cPSON* find_template_from_list(PtrList* const list, const char* key, int key_len) {
    for (int i = 0; i < list->size; i++) {
        if (strncmp(list->ptr[i]->name, key, key_len) == 0) {
            return list->ptr[i];
        }
    }
    return NULL;
}

static void erase_dolor(char* ptr) {
    if (ptr[0] == '$') {
        for (int i = 0; ptr[i] != '\0'; i++) {
            ptr[i] = ptr[i + 1];
        }
    }
}
static cPSON_bool parse_object(cPSON* const parent, parse_buffer* const buffer, PtrList* const templates,
                               const cPSON* used_template) {
    if (char_at_offset(buffer) != '{') {
        return False;
    }
    buffer->offset++;

    if (used_template != NULL) {
        used_template = used_template->child;
    }
    cPSON* head = NULL;
    cPSON* cur = NULL;
    cPSON_bool got_error = False;
    do {
        cPSON* item = NULL;  //
        skip_whitespace_comment(buffer);
        // read member
        // read member-key
        if (!has_char(buffer)) {
            got_error = True;
            break;
        }
        if (char_at_offset(buffer) == ',') {
            buffer->offset++;
            continue;
        }
        if (char_at_offset(buffer) == '}') {
            buffer->offset++;
            break;
        }
        item = cPSON_New_Item(&(buffer->hooks));
        if (used_template != NULL) {
            item->name = used_template->name;
            item->flag |= cPSON_FLAG_NameRef;
            // parse value directory
            if (!parse_value(item, buffer, templates, used_template)) {
                got_error = True;
                break;
            }
            used_template = used_template->next;
        } else {
            // key of member
            char* ptr = read_until_split(buffer);
            item->name = ptr;

            cPSON* template = NULL;
            if (*ptr == '$') {
                erase_dolor(item->name);
                cPSON_PtrList_add(templates, item);
                item->flag |= cPSON_FLAG_IsTemplate;
            } else {
                int start, len;
                start = str_find_template_name(ptr, &len);
                if (start > 0) {
                    ptr[start - 1] = '\0';
                    template = find_template_from_list(templates, ptr + start, len);
                    // printf("find template %d:%s\n",len,template->name);
                    item->template_name = template->name;
                    if (template == NULL) {
                        printf("could not find template <%s> defination\n", ptr + start);
                        return False;
                    }
                }
            }

            // : [ { or
            char split = char_at_offset(buffer);
            if (split == ':') {
                buffer->offset++;
                skip_whitespace_comment(buffer);
                if (!parse_value(item, buffer, templates, template)) {
                    got_error = True;
                    break;
                }
            } else if (split == '{' || split == '[') {
                if (!parse_value(item, buffer, templates, template)) {
                    got_error = True;
                    break;
                }
            } else {
                got_error = True;
                break;
            }
        }
        // skip_whitespace_comment(buffer);
        if (cur) {
            cur->next = item;
            item->prev = cur;
        }
        if (head == NULL) {
            head = item;
        }
        cur = item;
    } while (1);
    if (got_error) {
        // printf("got unknow data at %d\n",buffer->offset);
        if (head) {
            cPSON_Release(head);
        }
        return False;
    }
    if (head != NULL) {
        head->prev = cur;
    }
    parent->child = head;
    parent->type = cPSON_Object;
    return True;
}
static cPSON_bool buffer_has_str(parse_buffer* const buffer, const char* str) {
    int len = strlen(str);
    if (buffer->offset + len < buffer->length) {
        if (strncmp(str, (const char*)(buffer->content + buffer->offset), len) == 0) {
            return True;
        }
    }
    return False;
}
// parse object
static cPSON_bool parse_value(cPSON* const item, parse_buffer* const buffer, PtrList* const templates,
                              const cPSON* used_template) {
    // parse const- value
    if (buffer_has_str(buffer, "null")) {
        item->type = cPSON_Null;
        buffer->offset += 4;
        return True;
    }
    if (buffer_has_str(buffer, "true")) {
        item->type = cPSON_True;
        buffer->offset += 4;
        return True;
    }
    if (buffer_has_str(buffer, "on")) {
        item->type = cPSON_True;
        buffer->offset += 2;
        return True;
    }
    if (buffer_has_str(buffer, "false")) {
        item->type = cPSON_False;
        buffer->offset += 5;
        return True;
    }
    if (buffer_has_str(buffer, "off")) {
        item->type = cPSON_False;
        buffer->offset += 3;
        return True;
    }
    // parse const-type-value
    // String Number,Bool,Array,Object
    if (buffer_has_str(buffer, "String")) {
        item->type = cPSON_String;
        buffer->offset += 6;
        return True;
    }
    if (buffer_has_str(buffer, "Bool")) {
        item->type = cPSON_True;
        buffer->offset += 4;
        return True;
    }
    if (buffer_has_str(buffer, "Number")) {
        item->type = cPSON_Number;
        buffer->offset += 6;
        return True;
    }
    if (buffer_has_str(buffer, "Array")) {
        item->type = cPSON_Array;
        buffer->offset += 5;
        return True;
    }
    if (buffer_has_str(buffer, "Object")) {
        item->type = cPSON_Object;
        buffer->offset += 6;
        return True;
    }
    // string
    if (char_at_offset(buffer) == '\"' || char_at_offset(buffer) == '\'') {
        return parse_string(item, buffer);
    }
    // number
    if (can_read(buffer, 0) &&
        ((char_at_offset(buffer) == '-') || (char_at_offset(buffer) >= '0' && char_at_offset(buffer) <= '9'))) {
        return parse_number(item, buffer);
    }
    // object
    if (char_at_offset(buffer) == '{') {
        return parse_object(item, buffer, templates, used_template);
    }
    // array
    if (char_at_offset(buffer) == '[') {
        return parse_array(item, buffer, templates, used_template);
    }
    printf("got unknow data at %d :%s\n", buffer->offset, buffer_at_offset(buffer));
    return False;
}

cPSON* cPSON_Parse(const char* value) {
    size_t length = strlen(value);
    return cPSON_ParseWithLength(value, length);
}

cPSON* cPSON_ParseWithLength(const char* value, size_t length) {
    cPSON* item = NULL;
    parse_buffer buffer;
    buffer.content = value;
    buffer.length = length;
    buffer.offset = 0;
    buffer.hooks = global_hooks;
    item = cPSON_New_Item(&(buffer.hooks));

    PtrList* templates = cPSON_NewList();
    if (!parse_value(item, skip_whitespace_comment(skip_utf8_bom(&buffer)), templates, NULL)) {
        printf("got unknow data at %d\n", buffer.offset);
        cPSON_Release(item);
        item = NULL;
    }
    skip_whitespace_comment(&buffer);
    if (has_char(&buffer)) {
        printf("got some string left:%s|\n", buffer_at_offset(&buffer));
        // TODO
    }
    for (int i = 0; i < templates->size; i++) {
        printf("debug<%s>:%d\n", templates->ptr[i]->name, templates->ptr[i]->type);
    }
    cPSON_ReleaseList(templates);
    return item;
}

cPSON* cPSON_Search(cPSON* root, const char* path) {
    cPSON* item = NULL;
    // split path by /

    return item;
}
void cPSON_Release(cPSON* item) {
    cPSON* next = NULL;
    while (item) {
        next = item->next;
        if (item->name && !(item->flag & cPSON_FLAG_NameRef)){
            global_hooks.deallocate(item->name);
        }

        if ((item->type == cPSON_String || item->type == cPSON_Raw) && item->valuestring) {
            global_hooks.deallocate(item->valuestring);
        }
        if ((item->type == cPSON_Object || item->type == cPSON_Array)&&item->child) {
            cPSON_Release(item->child);
        }
        global_hooks.deallocate(item);
        item = next;
    }
}

CPSON_PUBLIC void cPSON_MemFree(void* ptr) {
    if (ptr) {
        global_hooks.deallocate(ptr);
    }
}

CPSON_PUBLIC int cPSON_GetArraySize(const cPSON* const array) {
    if (array && array->type == cPSON_Array) {
        int count = 0;
        cPSON* child = array->child;
        while (child) {
            count++;
            child = child->next;
        }
        return count;
    }
    return -1;
}

CPSON_PUBLIC cPSON* cPSON_GetArrayItem(const cPSON* array, int index) {
    if (array && array->type == cPSON_Array) {
        int count = 0;
        cPSON* child = array->child;
        while (child) {
            if (count == index) {
                return child;
            }
            count++;
            child = child->next;
        }
    }
    return NULL;
}

CPSON_PUBLIC cPSON* cPSON_GetObjectItem(const cPSON* object, const char* key) {
    if (object && object->type == cPSON_Object) {
        cPSON* child = object->child;
        while (child) {
            if (strcmp(key, child->name) == 0) {
                return child;
            }
            child = child->next;
        }
    }
    return NULL;
}


CPSON_PUBLIC char* cPSON_GetStringValue(const cPSON* const item){
    if(cPSON_IsString(item)){
        return item->valuestring;
    }
    return NULL;
}
CPSON_PUBLIC double cPSON_GetNumberValue(const cPSON* const item){
    if(cPSON_IsNumber(item)){
        return item->valuedouble;
    }
    return (double) NAN;
}
CPSON_PUBLIC cPSON_bool cPSON_GetBoolValue(const cPSON* const item){
    if(cPSON_IsTrue(item)){
        return cPSON_True;
    }
    return cPSON_False;
}

cPSON_bool cPSON_IsInvalid(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Invalid) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsObject(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Object) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsArray(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Array) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsString(const cPSON* const obj) {
    if (obj && obj->type == cPSON_String) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsNumber(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Number) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsTrue(const cPSON* const obj) {
    if (obj && obj->type == cPSON_True) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsFalse(const cPSON* const obj) {
    if (obj && obj->type == cPSON_False) {
        return True;
    }
    return False;
}
cPSON_bool cPSON_IsBool(const cPSON* const obj) {
    if (obj && (obj->type == cPSON_True || obj->type == cPSON_False)) {
        return True;
    }
    return False;
}

CPSON_PUBLIC cPSON_bool cPSON_IsRaw(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Raw) {
        return True;
    }
    return False;
}

CPSON_PUBLIC cPSON_bool cPSON_IsTemplate(const cPSON* const obj) {
    if (obj && obj->type == cPSON_Object) {
        return True;
    }
    return False;
}
// create
static char* cPSON_strdup(const char* str, const internal_hooks* const hooks) {
    int size = strlen(str);
    char* ptr = hooks->allocate(size + 1);
    if (ptr) {
        memcpy(ptr, str, size + 1);
    }
    return ptr;
}

CPSON_PUBLIC cPSON* cPSON_CreateNull() {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_Null;
    }
    return item;
}

CPSON_PUBLIC cPSON* cPSON_CreateObject() {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_Object;
    }
    return item;
}
CPSON_PUBLIC cPSON* cPSON_CreateArray() {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_Array;
    }
    return item;
}
CPSON_PUBLIC cPSON* cPSON_CreateTrue() {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_True;
    }
    return item;
}
CPSON_PUBLIC cPSON* cPSON_CreateFalse() {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_False;
    }
    return item;
}
CPSON_PUBLIC cPSON* cPSON_CreateBool(cPSON_bool val) {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = val ? cPSON_True : cPSON_False;
    }
    return item;
}

CPSON_PUBLIC cPSON* cPSON_CreateNumber(double number) {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_Number;
        item->valuedouble = number;
    }
    return item;
}

CPSON_PUBLIC cPSON* cPSON_CreateTemplate(const char* name) {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->name = cPSON_strdup(name, &global_hooks);
        ;
        item->type = cPSON_Object;  /// | cPSON_Template;
        item->flag |= cPSON_FLAG_IsTemplate;
    }
    return item;
}
CPSON_PUBLIC cPSON* cPSON_CreateObjectFromTemplate(const cPSON* template) {
    // deep copy template object.
    return NULL;
}
static void append_object(cPSON* prev, cPSON* item) {
    prev->next = item;
    item->prev = prev;
}
CPSON_PUBLIC cPSON* cPSON_CreateString(const char* string) {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_String;
        item->valuestring = cPSON_strdup(string, &global_hooks);
    }
    return item;
}

CPSON_PUBLIC cPSON* cPSON_CreateRaw(const char* string) {
    cPSON* item = cPSON_New_Item(&global_hooks);
    if (item) {
        item->type = cPSON_Raw;
        item->valuestring = cPSON_strdup(string, &global_hooks);
        if (!item->valuestring) {
            cPSON_Release(item);
            return NULL;
        }
    }
    return item;
}

static cPSON_bool add_item_to_array(cPSON* object, cPSON* item) {
    if (object && item) {
        if (object->child == NULL) {
            object->child = item;
            object->child->prev = item;
        } else {
            append_object(object->child->prev, item);
            object->child->prev = item;
            item->next = NULL;
        }
        return True;
    }
    return False;
}
static cPSON_bool add_item_to_object(cPSON* object, const char* key, cPSON* item, const internal_hooks* const hooks) {
    if (object && item) {
        char* new_key = cPSON_strdup(key, hooks);
        if (new_key == NULL) {
            return False;
        }
        item->name = new_key;
        if (object->child == NULL) {
            object->child = item;
            object->child->prev = item;
        } else {
            append_object(object->child->prev, item);
            object->child->prev = item;
            item->next = NULL;
        }
        return True;
    }
    return False;
}
CPSON_PUBLIC cPSON_bool cPSON_AddItemToArray(cPSON* object, cPSON* item) { return add_item_to_array(object, item); }
CPSON_PUBLIC cPSON_bool cPSON_AddItemToObject(cPSON* object, const char* key, cPSON* item) {
    // todo check used templated.

    cPSON_bool ret = add_item_to_object(object, key, item, &global_hooks);
    if (ret) {
        int start, len;
        start = str_find_template_name(item->name, &len);
        if (start > 0) {
            // item->flag = cPSON_FLAG_UseTemplate;
            item->name[start - 1] = '\0';  // <
            item->template_name = item->name + start;
            item->template_name[strlen(item->template_name) - 1] = '\0';  // >
        }
    }
    return ret;
}

// help function
CPSON_PUBLIC cPSON* cPSON_AddNumberToObject(cPSON* object, const char* name, double value) {
    cPSON* item = cPSON_CreateNumber(value);
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}
CPSON_PUBLIC cPSON* cPSON_AddStringToObject(cPSON* object, const char* name, const char* const value) {
    cPSON* item = cPSON_CreateString(value);
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}
CPSON_PUBLIC cPSON* cPSON_AddBoolToObject(cPSON* object, const char* name, cPSON_bool value) {
    cPSON* item = cPSON_CreateBool(value);
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}
CPSON_PUBLIC cPSON* cPSON_AddObjectToObject(cPSON* object, const char* name) {
    cPSON* item = cPSON_CreateObject();
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}
CPSON_PUBLIC cPSON* cPSON_AddArrayToObject(cPSON* object, const char* name) {
    cPSON* item = cPSON_CreateArray();
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}

CPSON_PUBLIC cPSON* cPSON_AddRawToObject(cPSON* object, const char* name, const char* const value) {
    cPSON* item = cPSON_CreateRaw(value);
    add_item_to_object(object, name, item, &global_hooks);
    return item;
}
CPSON_PUBLIC cPSON* cPSON_AddTemplateToObject(cPSON* object, const char* name) {
    cPSON* item = cPSON_CreateTemplate(name);
    add_item_to_array(object, item);
    return item;
}

CPSON_PUBLIC cPSON_bool cPSON_SetStringValue(cPSON* item, const char* string) {
    if (item && item->type == cPSON_String) {
        item->valuestring = cPSON_strdup(string, &global_hooks);
        return True;
    }
    return False;
}
CPSON_PUBLIC cPSON_bool cPSON_SetNumberValue(cPSON* item, double number) {
    if (item && item->type == cPSON_Number) {
        item->valuedouble = number;
        return True;
    }
    return False;
}
CPSON_PUBLIC cPSON_bool cPSON_SetBoolValue(cPSON* item, cPSON_bool value) {
    if (item && (item->type == cPSON_True || item->type == cPSON_False)) {
        item->type = value ? cPSON_True : cPSON_False;
        return True;
    }
    return False;
}

CPSON_PUBLIC cPSON_bool cPSON_SetObjectStringValue(cPSON* object, const char* key, const char* string) {
    if (object && object->child) {
        // find item
        cPSON* child = object->child;
        while (child) {
            if (strcmp(child->name, key) == 0) {
                if (child->type == cPSON_String) {
                    child->valuestring = cPSON_strdup(string, &global_hooks);
                    ;
                    return True;
                } else {
                    printf("key not match type\n");
                    return False;
                }
            }
            child = child->next;
        }
    }
    return False;
}
CPSON_PUBLIC cPSON_bool cPSON_SetObjectNumberValue(cPSON* object, const char* key, double value) {
    if (object && object->child) {
        // find item
        cPSON* child = object->child;
        while (child) {
            if (strcmp(child->name, key) == 0) {
                if (child->type == cPSON_Number) {
                    child->valuedouble = value;
                    return True;
                } else {
                    printf("key not match type\n");
                    return False;
                }
            }
            child = child->next;
        }
    }
    return False;
}
CPSON_PUBLIC cPSON_bool cPSON_SetObjectBoolValue(cPSON* object, const char* key, cPSON_bool value) {
    if (object && object->child) {
        // find item
        cPSON* child = object->child;
        while (child) {
            if (strcmp(child->name, key) == 0) {
                if (child->type == cPSON_True || child->type == cPSON_False) {
                    child->type = value ? cPSON_True : cPSON_False;
                    return True;
                } else {
                    printf("key not match type\n");
                    return False;
                }
            }
            child = child->next;
        }
    }
    return False;
}
// dump

typedef struct {
    char* buffer;
    size_t length;
    size_t offset;
    internal_hooks hooks;
    cPSON* root;
} printbuffer;

static unsigned char* ensure(printbuffer* const p, size_t needed) {
    int size = strlen(p->buffer);
    int left = p->length - size;
    if (left < needed) {
        static const size_t default_buffer_size = 256;
        int newsize = size + (needed > default_buffer_size ? needed : default_buffer_size);
        // enlarge
        if (p->hooks.reallocate) {
            p->buffer = (char*)p->hooks.reallocate(p->buffer, newsize);
            p->length = newsize;
        } else {
            char* tmp = (char*)p->hooks.allocate(newsize);
            memcpy(tmp, p->buffer, size);
            p->hooks.deallocate(p->buffer);
            p->buffer = tmp;
            p->length = newsize;
        }
    }
    // printf("ensure %d\n",size);
    return p->buffer + size;
}
// append str to end.
static void append_str(printbuffer* const p, const char* str) {
    char* buf = ensure(p, strlen(str) + 1);
    strcpy(buf, str);
}
/* securely comparison of floating-point variables */
static cPSON_bool compare_double(double a, double b){
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}
static void append_number(printbuffer* const p, double valuedouble) {
    char number_buffer[26]={0};
    int length =0;
    if (isnan(valuedouble) || isinf(valuedouble)) {
        length = sprintf((char*)number_buffer, "null");
    }else if ((double)(int)valuedouble == valuedouble) {
        length = sprintf(number_buffer, "%d", (int)valuedouble);
    } else {
        double test = 0.0;
        length = sprintf((char*)number_buffer, "%1.15g", valuedouble);
        /* Check whether the original double can be recovered */
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || !compare_double((double)test, valuedouble)) {
            /* If not, print with 17 decimal places of precision */
            length = sprintf((char*)number_buffer, "%1.17g", valuedouble);
        }
    }
    char* output = ensure(p, length+1);
    strcpy(output,number_buffer);
}
// process escape code,then append to end.
static void append_str_val(printbuffer* const p, const char* str) {
    int str_len = strlen(str);
    int black_splash_count = 0;
    for (int i = 0; i < str_len; i++) {
        if (str[i] == '\'' || str[i] == '\\' || str[i] == '\b' || str[i] == '\f' || str[i] == '\r' || str[i] == '\n' ||
            str[i] == '\t') {
            black_splash_count++;
        }
    }
    char* output = ensure(p, str_len + black_splash_count + 4);

    if (black_splash_count == 0) {
        sprintf(output, "'%s'\n", str);
    } else {
        *output++ = '\'';
        for (int i = 0; i < str_len; i++) {
            switch (str[i]) {
                case '\'':
                    *output++ = '\\';
                    *output++ = '\'';
                    break;
                case '\\':
                    *output++ = '\\';
                    *output++ = '\\';
                    break;
                case '\b':
                    *output++ = '\\';
                    *output++ = 'b';
                    break;
                case '\f':
                    *output++ = '\\';
                    *output++ = 'f';
                    break;
                case '\r':
                    *output++ = '\\';
                    *output++ = 'r';
                    break;
                case '\n':
                    *output++ = '\\';
                    *output++ = 'n';
                    break;
                case '\t':
                    *output++ = '\\';
                    *output++ = 't';
                    break;
                default:
                    *output++ = str[i];
            }
        }
        *output++ = '\'';
        *output++ = '\n';
        *output++ = '\0';
    }
}
static void append_space(printbuffer* const p, int n) {
    if (n > 0) {
        ensure(p, n + 1);
        int size = strlen(p->buffer);
        for (int i = 0; i < n; i++) {
            p->buffer[size + i] = ' ';
        }
        p->buffer[size + n] = '\0';
    }
}

static cPSON_bool sort_array_object_members(cPSON* array, const cPSON* template);

// make sure object with template has same member order.
static cPSON_bool sort_object_members(cPSON* object, const cPSON* template) {
    // printf("sort_object_members(%d) %s<<%s>>  %s\n",object->type,object->name, object->template_name,template->name);
    if (object->template_name == NULL) return False;

    cPSON* child = object->child;
    cPSON* temp = template->child;
    while (child) {
        // printf("sort member %s-%s\n",child->name, temp->name);
        if (strcmp(child->name, temp->name) != 0) {
            cPSON* dst = cPSON_GetObjectItem(object, temp->name);
            // move dst to before child.
            printf("\t%s\n", dst->name);
            if (dst) {
                cPSON* prev = dst->prev;
                cPSON* next = dst->next;
                // remove dst
                prev->next = next;
                if (next) {
                    next->prev = prev;
                } else {
                    template->child->prev = prev;
                }
                // insert dst
                prev = child->prev;
                if (prev->next == NULL) {
                    object->child = dst;  // head.
                } else {
                    prev->next = dst;
                }
                dst->prev = prev;
                dst->next = child;
                child->prev = dst;
                if (child->type == cPSON_Object) {
                    sort_object_members(child, temp);
                }else if (child->type == cPSON_Array) {
                    sort_array_object_members(child, temp);
                }

                child = dst;
            } else {
                printf("not found item for template\n");
            }
        }
        child = child->next;
        temp = temp->next;
    }
    return True;
}

static cPSON_bool sort_array_object_members(cPSON* array, const cPSON* template) {
    cPSON* child = array->child;
    while (child) {
        sort_object_members(child, template);
        child = child->next;
    }
    return True;
}
// search template object by name from root object.
static cPSON* find_template(cPSON* root, const char* name) {
    // printf("template %s\n",name);
    if (name) {
        cPSON* child = root->child;
        while (child) {
            // printf("\tfind template %s %s\n",child->name,name);
            if (child->name && (child->flag & cPSON_FLAG_IsTemplate) && strcmp(child->name, name) == 0) {
                return child;
            }
            child = child->next;
        }
    }
    return NULL;
}
// mini
// full
// format
static void print_item(cPSON* const item, int depth, printbuffer* const output_buffer, cPSON_bool is_template,
                       cPSON_bool is_in_template, int level) {
    if (!item) {
        printf("<nil>\n");
        return;
    }
    if (level == cPSON_Print_Mini) {
        depth = 0;
    }
    append_space(output_buffer, depth);
    if (item->name != NULL && is_in_template == False) {
        if (item->flag & cPSON_FLAG_IsTemplate) {
            is_template = True;
            char* output = ensure(output_buffer, strlen(item->name) + 3);
            sprintf(output, "$%s:", item->name);
        } else if (item->template_name != NULL || (item->name && item->name[strlen(item->name) - 1] == '>')) {
            is_in_template = True;
            cPSON* template = find_template(output_buffer->root, item->template_name);
            // sort members order with template.
            if (item->type == cPSON_Object) {
                sort_object_members(item, template);
            } else if (item->type == cPSON_Array) {
                sort_array_object_members(item, template);
            }
            char* output = ensure(output_buffer, strlen(item->name) + strlen(item->template_name) + 4);
            sprintf(output, "%s<%s>:", item->name, item->template_name);
        } else {
            char* output = ensure(output_buffer, strlen(item->name) + 2);
            sprintf(output, "%s:", item->name);
        }
    }

    switch (item->type & 0xFF) {
        case cPSON_Null:
            append_str(output_buffer, "null\n");
            break;
        case cPSON_True:
            if (is_template) {
                append_str(output_buffer, "Bool\n");
            } else {
                append_str(output_buffer, "true\n");
            }
            break;
        case cPSON_False:
            if (is_template) {
                append_str(output_buffer, "Bool\n");
            } else {
                append_str(output_buffer, "false\n");
            }
            break;
        case cPSON_String:
            if (is_template) {
                append_str(output_buffer, "String\n");
            } else {
                append_str_val(output_buffer, item->valuestring);
            }
            break;
        case cPSON_Number:
            if (is_template) {
                append_str(output_buffer, "Number\n");
            } else {
                append_number(output_buffer, item->valuedouble);
                append_str(output_buffer, "\n");
            }
            break;
        case cPSON_Object: {
            append_str(output_buffer, "{\n");
            cPSON* child = item->child;
            while (child) {
                print_item(child, depth + 1, output_buffer, is_template, is_in_template, level);
                child = child->next;
            }
            append_space(output_buffer, depth);
            append_str(output_buffer, "}\n");
            break;
        }
        case cPSON_Array: {
            append_str(output_buffer, "[\n");
            cPSON* child = item->child;
            while (child) {
                print_item(child, depth + 1, output_buffer, is_template, is_in_template, level);
                child = child->next;
            }
            append_space(output_buffer, depth);
            append_str(output_buffer, "]\n");
            break;
        }
        case cPSON_Raw: {
            if (item->valuestring == NULL) {
                return False;
            }
            append_str(output_buffer, item->valuestring);
            append_str(output_buffer, "\n");
        }
    }
}

static void print_item_full(cPSON* const item, int depth, printbuffer* const output_buffer) {
    if(item&&(item->flag& cPSON_FLAG_IsTemplate)){
        return ;
    }
    append_space(output_buffer, depth);
    if(item->name) {
        char* output = ensure(output_buffer, strlen(item->name) + 2);
        sprintf(output, "%s:", item->name);
    }
    switch (item->type & 0xFF) {
        case cPSON_Null:
            append_str(output_buffer, "null\n");
            break;
        case cPSON_True:
            append_str(output_buffer, "true\n");
            break;
        case cPSON_False:
            append_str(output_buffer, "false\n");
            break;
        case cPSON_String:
            append_str_val(output_buffer, item->valuestring);
            break;
        case cPSON_Number:
            append_number(output_buffer, item->valuedouble);
            append_str(output_buffer, "\n");
            break;
        case cPSON_Object: {
            append_str(output_buffer, "{\n");
            cPSON* child = item->child;
            while (child) {
                print_item_full(child, depth + 1, output_buffer);
                child = child->next;
            }
            append_space(output_buffer, depth);
            append_str(output_buffer, "}\n");
            break;
        }
        case cPSON_Array: {
            append_str(output_buffer, "[\n");
            cPSON* child = item->child;
            while (child) {
                print_item_full(child, depth + 1, output_buffer);
                child = child->next;
            }
            append_space(output_buffer, depth);
            append_str(output_buffer, "]\n");
            break;
        }
        case cPSON_Raw: {
            if (item->valuestring == NULL) {
                return False;
            }
            append_str(output_buffer, item->valuestring);
            append_str(output_buffer, "\n");
        }
    }
}
CPSON_PUBLIC void cPSON_Dump(cPSON* const item) {
    char* str = cPSON_Print(item, cPSON_Print_Normal);
    printf("%s\n", str);
    cPSON_MemFree(str);
}

CPSON_PUBLIC char* cPSON_Print(cPSON* const item, int level) {
    // buffer
    static const size_t default_buffer_size = 256;
    printbuffer buffer[1];
    memset(buffer, 0, sizeof(buffer));
    buffer->buffer = (char*)global_hooks.allocate(default_buffer_size);
    buffer->length = default_buffer_size;
    buffer->offset = 0;
    buffer->hooks = global_hooks;
    buffer->buffer[0] = '\0';
    buffer->root = item;

    if(level==cPSON_Print_Full){
        print_item_full(item, 0, buffer);
    }else{
        print_item(item, 0, buffer, False, False, level);
    }
    return buffer->buffer;
}