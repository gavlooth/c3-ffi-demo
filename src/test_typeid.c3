module main;
import std::io;

fn void check_typeids() {
    io::printfn("int.typeid = %d", (usz)int.typeid);
    io::printfn("float.typeid = %d", (usz)float.typeid);
    io::printfn("double.typeid = %d", (usz)double.typeid);
    io::printfn("bool.typeid = %d", (usz)bool.typeid);
    io::printfn("char.typeid = %d", (usz)char.typeid);
    io::printfn("uint.typeid = %d", (usz)uint.typeid);
    io::printfn("long.typeid = %d", (usz)long.typeid);
    io::printfn("ObjectHandle.typeid = %d", (usz)ObjectHandle.typeid);
    io::printfn("Region.typeid = %d", (usz)Region.typeid);
}
