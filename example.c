
// #include <fstream>
#include <stdio.h>
#include <stdlib.h>

#include "cpson.h"

int main(int argc, char* argv[]) {
    printf("pson \n");
    // parse
    cPSON* root = NULL;

    if(argc>1){
        FILE *fp = fopen(argv[1],"rb");
        fseek(fp,0,SEEK_END);
        long size = ftell(fp);
        fseek(fp,0,SEEK_SET);
        char* data = (char*) malloc(size);
        fread(data,1,size,fp);
        fclose(fp);
        root = cPSON_ParseWithLength(data,size);
    }else{
        root = cPSON_Parse("# comment\n {   name:'jack' alias:\"Jakkk\"  id:32423 scores[true false ],node:{email:'abc@def.com'}}");
    }


    printf("parse %p \n", root );
    if(root){
        cPSON_Dump(root);
        // 
        cPSON_Release(root);
    }
    if(1){
        cPSON* pson = cPSON_CreateObject();
        // cPSON* temp = cPSON_CreateTemplate("Book");
        // cPSON_AddItemToObject(pson, temp->name, temp);
        cPSON* temp = cPSON_AddTemplateToObject(pson,"Book");
        cPSON_AddStringToObject(temp,"name",""); 
        cPSON_AddNumberToObject(temp,"price",4);

        cPSON* book = cPSON_CreateObject();
        cPSON_AddNumberToObject(book,"price",30);
        cPSON_AddStringToObject(book,"name","book's name");
        cPSON_AddItemToObject(pson,"book<Book>",book);

        cPSON* age = cPSON_CreateNumber(30.123);
        cPSON_AddItemToObject(pson,"age",age);

        cPSON* array = cPSON_CreateArray();
        cPSON* ch = cPSON_CreateString("China");
        cPSON_AddItemToArray(array,ch);
        cPSON* jp = cPSON_CreateString("Japan");
        cPSON_AddItemToArray(array,jp);
        cPSON_AddItemToObject(pson,"language",array);

        cPSON_AddRawToObject(pson,"raw","{id:12 score:0.99 ret:true name:'name<abc>.' }");
        printf("ok\n");
        // cPSON_Dump(pson);
        char* str = cPSON_Print(pson, cPSON_Print_Full);
        cPSON_Release(pson);

        printf("cPSON_Print:\n%s\n",str);
        {
           cPSON* root = cPSON_Parse(str);
           printf("xxxxxxxxxxxxxxxxxxxxx\n");
           cPSON_Dump(root);
           cPSON_Release(root);

        }
        cPSON_MemFree(str);
        
    }
    return 0;
}
// mini  单行，包含模板
// normal 多行，模板
// full  多行，无模板

// 模板 顺序
// -> -> 模板关系