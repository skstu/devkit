//jsonnet --ext-str word="hello world" test.jsonnet
//jsonnet --tla-str word="hello world" test.jsonnet
/*
$ npm install jsonnet --save
 
var Jsonnet = require('jsonnet');
// instance jsonnet
var jsonnet = new Jsonnet();
var fs = require('fs');
 
var code = fs.readFileSync("./menu.jsonnet");
 
// eval jsonnet to javascript object
var result = jsonnet.eval(code);
 
console.log(result);

*/



/*function(word){
    txt: std.parseInt(word)
}*/

local num = 3;

//!@ 一个完整的对象
local object = {

};
local obj = {
inputs : [
{
    value: std.parseInt(std.extVar('word')) 
},
{
    value: 2
}
],
output:{
    value: 1
}
};

{ 
    key: obj, 
    'key with space': 'key with special char should be quoted' 
}
