#ifndef C_PSON__h
#define C_PSON__h
#endif

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define cPSON_Invalid (0)
#define cPSON_False (1 << 0)
#define cPSON_True (1 << 1)
#define cPSON_Null (1 << 2)
#define cPSON_Number (1 << 3)
// #define cPSON_Integer (1 << 3)
// #define cPSON_Number (1 << 3)
#define cPSON_String (1 << 4)
#define cPSON_Array (1 << 5)
#define cPSON_Object (1 << 6)
#define cPSON_Raw (1 << 7) /* raw json */

#define cPSON_Template (1 << 8)          // template
#define cPSON_FLAG_IsTemplate (1 << 0)   // template
#define cPSON_FLAG_UseTemplate (1 << 1)  // template
#define cPSON_FLAG_NameRef (1 << 2)      // template
#define cPSON_FLAG_StringRef (1 << 3)    // template

// #define cPSON_IsReference 256

typedef int cPSON_bool;

#define True ((cPSON_bool)1)
#define False ((cPSON_bool)0)

#define CPSON_STDCALL __stdcall
#ifdef __WINDOWS__
#define CPSON_PUBLIC __declspec(dllexport)
#else
#define CPSON_PUBLIC
#endif

typedef struct cPSON {
    // value type, one of cPSON_False,cPSON_True,cPSON_Null,cPSON_Number,cPSON_String,cPSON_Object,cPSON_Array,cPSON_Raw.
    unsigned short type;
    // option flag: cPSON_FLAG_XXX; Template, RefValue?
    unsigned short flag;  

    struct cPSON* prev;   // previes sibling or last one.
    struct cPSON* next;   // next sibling or NULL

    union{
        // array or object item values when type is cPSON_Object or cPSON_Array
        struct cPSON* child; 
        // The string value, when type is cPSON_String or cPSON_Raw 
        char* valuestring;
        // The number value when type is cPSON_Number
        double valuedouble;
    };
    // key of value, or null in array and top-object. It's a reference when (flag&cPSON_FLAG_NameRef).
    char* name;
    // template name if used. It's a reference.
    char* template_name;  
} cPSON;

// parse string,
CPSON_PUBLIC cPSON* cPSON_Parse(const char* value);
CPSON_PUBLIC cPSON* cPSON_ParseWithLength(const char* value, size_t length);

// check value type.
CPSON_PUBLIC cPSON_bool cPSON_IsInvalid(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsString(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsNumber(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsTrue(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsFalse(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsBool(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsNull(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsObject(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsArray(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsRaw(const cPSON* const obj);
CPSON_PUBLIC cPSON_bool cPSON_IsTemplate(const cPSON* const obj);

// check item type, and return value
CPSON_PUBLIC char* cPSON_GetStringValue(const cPSON* const item);
CPSON_PUBLIC double cPSON_GetNumberValue(const cPSON* const item);
CPSON_PUBLIC cPSON_bool cPSON_GetBoolValue(const cPSON* const item);

CPSON_PUBLIC int cPSON_GetArraySize(const cPSON* const array);
// find member by index.
CPSON_PUBLIC cPSON* cPSON_GetArrayItem(const cPSON* array, int index);
// find member(key:value) by name
CPSON_PUBLIC cPSON* cPSON_GetObjectItem(const cPSON* object, const char* key);

// create value.
CPSON_PUBLIC cPSON* cPSON_CreateNull();
CPSON_PUBLIC cPSON* cPSON_CreateObject();
CPSON_PUBLIC cPSON* cPSON_CreateArray();
CPSON_PUBLIC cPSON* cPSON_CreateTrue();
CPSON_PUBLIC cPSON* cPSON_CreateFalse();
CPSON_PUBLIC cPSON* cPSON_CreateBool(cPSON_bool val);
CPSON_PUBLIC cPSON* cPSON_CreateNumber(double number);
CPSON_PUBLIC cPSON* cPSON_CreateString(const char* string);
CPSON_PUBLIC cPSON* cPSON_CreateRaw(const char* string);
CPSON_PUBLIC cPSON* cPSON_CreateTemplate(const char* name);
CPSON_PUBLIC cPSON* cPSON_CreateObjectFromTemplate(const cPSON* template);

// set item value
CPSON_PUBLIC cPSON_bool cPSON_SetStringValue(cPSON* item, const char* string);
CPSON_PUBLIC cPSON_bool cPSON_SetNumberValue(cPSON* item, double number);
CPSON_PUBLIC cPSON_bool cPSON_SetBoolValue(cPSON* item, cPSON_bool value);

// set object member item value.
CPSON_PUBLIC cPSON_bool cPSON_SetObjectStringValue(cPSON* object, const char* key, const char* value);
CPSON_PUBLIC cPSON_bool cPSON_SetObjectNumberValue(cPSON* object, const char* key, double value);
CPSON_PUBLIC cPSON_bool cPSON_SetObjectBoolValue(cPSON* object, const char* key, cPSON_bool value);

CPSON_PUBLIC cPSON_bool cPSON_AddItemToObject(cPSON* object, const char* string, cPSON* item);
CPSON_PUBLIC cPSON_bool cPSON_AddItemToArray(cPSON* object, cPSON* item);

// help function for creating and adding items to an object.
// same as the two funciton: cPSON_CreateXXX(), cPSON_AddItemToObject().
CPSON_PUBLIC cPSON* cPSON_AddNumberToObject(cPSON* object, const char* name, double value);
CPSON_PUBLIC cPSON* cPSON_AddStringToObject(cPSON* object, const char* name, const char* const value);
CPSON_PUBLIC cPSON* cPSON_AddBoolToObject(cPSON* object, const char* name, cPSON_bool value);
CPSON_PUBLIC cPSON* cPSON_AddObjectToObject(cPSON* object, const char* name);
CPSON_PUBLIC cPSON* cPSON_AddArrayToObject(cPSON* object, const char* name);
CPSON_PUBLIC cPSON* cPSON_AddRawToObject(cPSON* object, const char* name, const char* const value);
CPSON_PUBLIC cPSON* cPSON_AddTemplateToObject(cPSON* object, const char* name);

/// search certain value from the root PSON.
/// path : /key or /key0/key1/key2
/// for example :
/// in pson: { person:{name:'jack',id:12}}
/// search by key: /person/name, will return cPSON item with value 'jack'.
CPSON_PUBLIC cPSON* cPSON_Search(cPSON* root, const char* path);

CPSON_PUBLIC void cPSON_Release(cPSON* root);
CPSON_PUBLIC void cPSON_MemFree(void* ptr);

CPSON_PUBLIC void cPSON_Dump(cPSON* const item);

// multi lines, with templates.
#define cPSON_Print_Normal 0
// multi lines, without templates.
#define cPSON_Print_Full 1
// single line with templates
#define cPSON_Print_Mini 2
CPSON_PUBLIC char* cPSON_Print(cPSON* const item, int level);

#ifdef __cplusplus
}
#endif