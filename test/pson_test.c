
#include <assert.h>
#include "cpson.h"


void test_parse_number(){
    cPSON* root = cPSON_Parse("{integer:100 float:100.001}");
    // assert(cPSON_IsNumber(root));
    cPSON* num = cPSON_GetObjectItem(root,"integer");
    assert(cPSON_GetNumberValue(num)==100);
    cPSON* flt = cPSON_GetObjectItem(root,"float");
    assert(cPSON_GetNumberValue(flt)==100.001);
    cPSON_Release(root);
}

void test_parse_string(){
    cPSON* root = cPSON_Parse("{str0:'this is string' str1:\"this is string\"}");
    // assert(cPSON_IsNumber(root));
    cPSON* str0 = cPSON_GetObjectItem(root,"str0");
    assert(strcmp(cPSON_GetStringValue(str0),"this is string")==0);
    
    cPSON* str1 = cPSON_GetObjectItem(root,"str1");
    assert(strcmp(cPSON_GetStringValue(str1),"this is string")==0);
    cPSON_Release(root);
}

void test_parse_bool(){
    //bool0:true bool1:on bool2:false 
    cPSON* root = cPSON_Parse("{bool0:true bool1:on bool2:false bool3:off}");
    
    cPSON* bool0 = cPSON_GetObjectItem(root,"bool0");
    assert(cPSON_GetBoolValue(bool0)==cPSON_True);

    cPSON* bool1 = cPSON_GetObjectItem(root,"bool1");
    assert(cPSON_GetBoolValue(bool1)==cPSON_True);

    cPSON* bool2 = cPSON_GetObjectItem(root,"bool2");
    assert(cPSON_GetBoolValue(bool2)==cPSON_False);
    cPSON* bool3 = cPSON_GetObjectItem(root,"bool3");
    assert(cPSON_GetBoolValue(bool3)==cPSON_False);
    cPSON_Release(root);
}

void test_parse_object(){
    cPSON* root = cPSON_Parse("{name:'Name',id:0,flag:false}");
    assert(cPSON_IsObject(root));
    cPSON_Release(root);
}

void test_parse_array(){
    // cPSON* root = cPSON_Parse("{}");
    // cPSON_GetObjectItem(root,"integer");
    // assert(cPSON_IsObject(root));
    // cPSON_Release(root);
}
int main(int argc, char * argv[]){

    test_parse_object();
    test_parse_number();

    test_parse_string();

    test_parse_bool();

    return 0;
}