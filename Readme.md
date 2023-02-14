
# pson_c

pson_c pson parse and generate library written by c language.

## parse
```c
    cPSON* root = cPSON_Parse("{}");
    
    
    cPSON_Release(root);
```

## generate

```c
    // create root pson object
    cPSON* root = cPSON_CreateObject();

    // add number key-value pari.
    cPSON* age = cPSON_CreateNumber(30.123);
    // after add, the age's memory will owned by root
    cPSON_AddItemToObject(root,"age0",age);
    // or 
    cPSON_AddNumberToObject(root,"age1",30.123)

    // object
    cPSON* book = cPSON_CreateObject();
    cPSON_AddNumberToObject(book,"price",30);
    cPSON_AddStringToObject(book,"name","book's name");
    cPSON_AddItemToObject(pson,"name",book);

    // array
    cPSON* array = cPSON_CreateArray();
    cPSON* ch = cPSON_CreateString("China");
    cPSON_AddItemToArray(array,ch);
    cPSON* jp = cPSON_CreateString("Japan");
    cPSON_AddItemToArray(array,jp);
    cPSON_AddItemToObject(root,"language",array);

    // add raw value.
    cPSON_AddRawToObject(root,"raw","{id:12 score:0.99 ret:true name:'name<abc>.' }");

    char* str = cPSON_Print(root, cPSON_Print_Normal);
    // release memory
    cPSON_Release(root);

    printf("%s\n",str);

    cPSON_MemFree(str);
```

## templates

## parse with tempale
define a template.
```c
```

## generate wieth template.