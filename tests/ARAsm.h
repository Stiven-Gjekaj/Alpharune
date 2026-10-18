// ARAsm: assembles GML bytecode (version 17) into a small data file, for
// the tests of the VM.
//
// Each line of the source is one instruction or a label:
//
//     pushi 5                 push.i 7         push.d 0.5     push.l 9
//     push.s "text"           push.i func:name (a function reference)
//     push.v -1.x             push.v 0.a array pushglb.v -5.g  pushloc.v -7.n
//     pop.v.v -1.x            pop.i.v 0.a array               pop.v.v 0.b stacktop
//     conv.i.v  add.v.v  cmp.v.v lt  neg.v  not.b  dup.v 0  dup.i 4 swap176
//     b loop  bt end  bf end  pushenv end  popenv body  popenv drop
//     call name 2   callv 1   ret.v   exit.i   popz.v   break -5
//     loop:
//
// The variable kinds after a name are array, stacktop, pushaf, and popaf.

#ifndef AR_ASM_H
#define AR_ASM_H

#include <stddef.h>
#include <stdint.h>

typedef struct ARAsm ARAsm;

ARAsm *ar_asm_new(void);
void ar_asm_free(ARAsm *a);

// Adds an object. parent is -1 for none. Returns its index.
int ar_asm_object(ARAsm *a, const char *name, int parent);
// Sets the code of an event of an object.
void ar_asm_event(ARAsm *a, int object, int type, int subtype, int code);
// Adds a room of w x h pixels. Returns its index. The first room is the
// first in the room order.
int ar_asm_room(ARAsm *a, const char *name, int w, int h, int creation_code);
// Adds an instance of an object to the last room. id 0 picks the next id.
// creation_code is -1 for none. Returns the id.
int ar_asm_instance(ARAsm *a, int object, int x, int y, int creation_code);
// Sets the sprite and the persistence of an object.
void ar_asm_object_sprite(ARAsm *a, int object, int sprite, int persistent);
// Adds a sprite of w x h pixels with a full box mask and one frame.
int ar_asm_sprite(ARAsm *a, const char *name, int w, int h, int origin_x, int origin_y);
// Adds a code entry. A name of "gml_Script_x" also adds the script
// "gml_Script_x". Returns the index of the code, or -1 for an error in the
// source (ar_asm_error tells it).
int ar_asm_code(ARAsm *a, const char *name, int arguments, const char *source);
// Adds a code entry that starts at a label of the code before it, as a
// function inside a global script.
int ar_asm_child(ARAsm *a, const char *name, int arguments, const char *label);
// Adds the code to the global scripts of the GLOB chunk.
void ar_asm_global_script(ARAsm *a, int code);
const char *ar_asm_error(ARAsm *a);

// Makes the data file. The bytes stay valid until ar_asm_free.
const uint8_t *ar_asm_finish(ARAsm *a, size_t *size);

#endif
