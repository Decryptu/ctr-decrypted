# Pointers and the 64-bit engine

The game code is decompiled from the PS1, where a pointer is 4 bytes, and it reads the disc's level, model and nav files in place. The rules below keep that working whatever the host's pointer size, so the same code builds as 32-bit x86 (today's release) and as 64-bit x86_64 or arm64 (`-DCTR_64BIT=ON`, see [ROADMAP.md](../ROADMAP.md) for its state).

## Two kinds of struct
- **Disc-format.** Its bytes come from a file: the LEV (`struct Level` and everything it reaches: `mesh_info`, `QuadBlock`, `BSP`, `PVS`, `VisMem` and its lists, `InstDef`, `AnimTex`, spawn tables, `Skybox`, icon packs), models (`Model`, `ModelHeader`, `ModelAnim`), MPK packs (`struct ModelPack`), nav paths (`NavHeader`). It keeps retail's byte layout on every build, and a pointer field in it is a `PSX_PTR(T)`.
- **Runtime.** Everything the game allocates and fills itself (`Instance`, `Driver`, `Thread`, `GameTracker`, `CameraDC`...). Plain C with native pointers. Its retail offsets and sizes are asserted with `CTR_RETAIL_LAYOUT_ASSERT`, which checks them where pointers are 4 bytes; a disc-format struct's use `CTR_STATIC_ASSERT` and hold on every build.

`Instance` is the one runtime struct a disc slot points at (`InstDef.ptrInstance`, the PVS instance lists); instances come from a pool inside PSX space.

## PSX space and PSX addresses
`include/ctr_psx_ptr.h`, `platform/native_psx_space.c`.
- PSX space (`gPsxSpace`) is one static host block: the mempack backing first (`platform/native_memory.c`), then a heap (`PsxSpace_Alloc`, `PsxSpace_Free`).
- A `PSX_PTR` holds a PSX address: `0x80000000` plus the target's offset in PSX space, or 0 for NULL. Under retail memory pressure the mempack backing is the 2 MiB PS1 RAM image, so stored values are the addresses retail keeps in RAM: compare level data with PCSX-Redux directly, and retail's sign tests on pointer words work unchanged.
- `PSX_GET(p)` gives the typed host pointer, `PSX_SET(p, v)` stores one (type-checked), `PSX_ADDR(p)` is the raw word. `PSX_PTR` is a union, so using a field without them does not compile: the compiler lists every site. The union is pinned to 4-byte alignment; its type-carrying member would otherwise give it a host pointer's 8 and pad every disc struct.
- `LOAD_RunPtrMap` adds the file's PSX address to each slot its pointer map names.
- A pointer to an array of PSX pointers is `Psx<Type>Ptr *` (`PsxModelPtr`, `PsxIconPtr`...): each element needs its own `PSX_GET`. Accessors for the arrays that follow a header: `ST1_GETPOINTER`, `ANIMTEX_GETFRAME`, `ICONGROUP_GETICON`.

## Rules
- **Disc-format data lives in PSX space.** A file read by the host and relocated (racer and bot models, Archipelago harvests, the Oxide menu model, the Archipelago title Roo), or disc-format data the host builds (Archipelago box, marker and beam models, recorded nav lanes, the blank nav template, missing VisMem lists), is allocated with `PsxSpace_Alloc`, never `malloc` or a static array. Storing a pointer outside PSX space is a fatal error that names it.
- **No raw reads of pointer slots.** `*(struct X **)p`, `(struct X *)*(u32 *)p` or a `memcpy` between a disc struct and a runtime struct read a PSX address as a host pointer. Go through the typed field or the accessors above. A copy between the two kinds is field by field (`INSTANCE.c`, level instance birth).
- **Records told apart by where they live.** The InstDef/Instance lists swap in place (`LevInstDef.c`); retail finds the peer at offset 0x2c of either, native knows an Instance by its pool.
- **Pools hold the 64-bit size.** A runtime struct is at most twice its retail size on a 64-bit host: every field at most doubles, and so does every offset. Thread object pools (`THREAD_*_STACK_SIZE`) hold twice retail's object there, and are retail's on 32-bit, so memory pressure and the bit-exact runs do not change.
- **No pointers in 32-bit integers.** The 64-bit build makes casts between pointers and narrower integers errors (`-Werror=pointer-to-int-cast`, `-Werror=int-to-pointer-cast`). Address arithmetic goes through `u8 *`, a field or variable that holds a pointer has a pointer type, and a word that holds a PSX address is a `PSX_PTR`.
- **Tables the game builds are host tables.** The language strings (`LOAD_LangFile_IndexStrings`) point a host array at the file's strings; the file keeps its offsets.

## Checking
- The 32-bit build must stay bit-exact through any change here: capture frames, `CTR_DEV_TRACE` and `CTR_DEV_RESULTS` with `tools/decrypted/devrun.ps1` (title menu, attract demos, autopilot races, the N. Sanity Beach hub, a Gem Cup) before and after, and compare the files byte for byte.
- A battle arena exercises the blank nav template; a converted custom track exercises the host VisMem lists and mover patterns. The Archipelago build's title screen exercises its relocated Roo model.
- Host harnesses link the real PSX space. One that includes engine code includes `platform/native_psx_space.c`; one whose fixture declares its own copies of disc-format structs includes `tools/harness-psx-space.h` (built with `-DCTR_NATIVE -I include`), declares their pointer fields with `PSX_PTR`, and allocates what they point at with `PsxSpace_Alloc`, so the production code under test runs on the same pointers as in the game.
