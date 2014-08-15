MODULE GeneratorWin64v2;

IMPORT
	CPmain, SYSTEM, Sys, Base;

CONST
	MIN_INT8 = -128;
	MAX_INT8 = 127;
	MAX_UINT32 = 4294967295;
	MAX_INT32 = 2147483647;
	MIN_INT32 = -2147483648;
	
	if_skipped = 0; if_REX = 1; if_16bit_prefix = 2; if_disp = 3;
	if_32bit_disp = 4;

	W_bit = 8; R_bit = 4; X_bit = 2; B_bit = 1; (* REX prefix *)
	d_bit = 2; w_bit = 1; s_bit = 2; w_bit_1byte = 8;

	reg_A = 0; reg_C = 1; reg_D = 2; reg_B = 3;
	reg_SP = 4; reg_BP = 5; reg_SI = 6; reg_DI = 7;
	reg_R8 = 8; reg_R9 = 9; reg_R10 = 10; reg_R11 = 11;
	reg_R12 = 12; reg_R13 = 13; reg_R14 = 14; reg_R15 = 15;
	
	MOVr = 88H; ADDr = 0; SUBr = 28H; IMULr = 0AF0FH;
	
	MOVZX = 0B60FH;
	
	ADDi = 80H; SUBi = 580H; IMULi = 69H;

TYPE
	InstructionInfo = RECORD
		flag : SET;
		ip : INTEGER;
		size : UBYTE
	END;

	Instruction = ARRAY 15 OF UBYTE;
	
	RegStack = RECORD
		ord : ARRAY 16 OF UBYTE;
		i, n : INTEGER
	END;
	
	Item* = RECORD
		r : UBYTE;
		mode, lev : INTEGER;
		type : Base.Type;
		b, c : INTEGER;
		a : LONGINT
	END;
	
VAR
	code : ARRAY 65536 OF Instruction;
	codeinfo : ARRAY 65536 OF InstructionInfo;
	pc, ip : INTEGER;
	
	reg_stacks : ARRAY 1 OF RegStack;
	crs : INTEGER;
	
	(* Global variables for Emit procedures *)
	Emit : RECORD
		i : UBYTE;
		iflag : SET;
	END;
	
PROCEDURE Mark (str : ARRAY OF CHAR);
BEGIN
END Mark;

PROCEDURE Emit_REX_prefix (rsize, reg, r2, bas, idx : UBYTE);
	VAR n : UBYTE;
BEGIN
	IF (rsize = 8) OR ({reg, r2, bas, idx} * {reg_R8 .. reg_R15} # {})
	OR (rsize = 1) & ({reg, r2} * {reg_SP..reg_DI} # {}) THEN
		n := 40H;
		IF reg >= reg_R8 THEN INC (n, R_bit) END;
		IF {r2, bas} * {reg_R8 .. reg_R15} # {} THEN INC (n, B_bit) END;
		IF idx >= reg_R8 THEN INC (n, X_bit) END;
		IF rsize = 8 THEN INC (n, W_bit) END;
		code [pc, Emit.i] := n; INCL (Emit.iflag, if_REX); INC (Emit.i)
	END
END Emit_REX_prefix;

PROCEDURE Emit_16bit_prefix (rsize : UBYTE);
BEGIN
	IF rsize = 2 THEN INCL (Emit.iflag, if_16bit_prefix);
		code [pc, Emit.i] := 66H; INC (Emit.i)
	END
END Emit_16bit_prefix;

PROCEDURE Emit_opcodeR (op : INTEGER; rsize, d : UBYTE);
BEGIN
	IF op MOD 256 = 0FH THEN
		code [pc, Emit.i] := 0FH; op := op DIV 256;
		INC (Emit.i)
	END;
	IF (rsize > 1) & (BITS(op) * BITS(w_bit) = {}) THEN INC (op, w_bit) END;
	IF (d > 0) & (BITS(op) * BITS(d_bit) = {}) THEN INC (op, d_bit) END;
	code [pc, Emit.i] := USHORT (op);
	INC (Emit.i)
END Emit_opcodeR;

PROCEDURE Emit_ModRM (reg, rm, bas : UBYTE; disp : INTEGER);
	VAR n : INTEGER;
BEGIN
	n := reg MOD 8 + rm;
	IF disp = 0 THEN
		IF (rm IN {reg_BP, reg_R13})
		OR (rm = reg_SP) & (bas IN {reg_BP, reg_R13}) THEN
			INCL (Emit.iflag, if_disp); INC (n, 40H)	(* mod = 01b *)
		END
	ELSIF (disp >= MIN_INT8) & (disp <= MAX_INT8) THEN
		INCL (Emit.iflag, if_disp); INC (n, 40H) (* mod = 01b *)
	ELSE
		Emit.iflag := Emit.iflag + {if_disp, if_32bit_disp};
		INC (n, 80H) (* mod = 10b *)
	END;
	code [pc, Emit.i] := USHORT (n);
	INC (Emit.i)
END Emit_ModRM;

PROCEDURE Emit_disp (disp : INTEGER);
BEGIN
	IF if_disp IN Emit.iflag THEN
		SYSTEM.PUT (SYSTEM.ADR (code [pc, Emit.i]), disp);
		IF if_32bit_disp IN Emit.iflag THEN INC (Emit.i, 4)
		ELSE INC (Emit.i)
		END
	END
END Emit_disp;

PROCEDURE Next_inst;
BEGIN
	codeinfo [pc].ip := ip; codeinfo [pc].size := Emit.i;
	codeinfo [pc].flag := Emit.iflag; INC (pc); INC (ip, Emit.i)
END Next_inst;

(* -------------------------------------------------------------------------- *)

PROCEDURE EmitRR (op : INTEGER; reg, rsize, rm : UBYTE);
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, reg, rm, 0, 0); Emit_16bit_prefix (rsize);
	Emit_opcodeR (op, rsize, 1);
	
	(* ModR/M byte *)
	code [pc, Emit.i] := USHORT (0C0H + reg MOD 8 * 8 + rm MOD 8);
	INC (Emit.i);
	
	Next_inst
END EmitRR;

PROCEDURE EmitRMs
(d : UBYTE; op : INTEGER; reg, rsize, bas, idx, scl : UBYTE; disp : INTEGER);
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, reg, 0, bas, idx); Emit_16bit_prefix (rsize);
	Emit_opcodeR (op, rsize, d); Emit_ModRM (reg, reg_SP, bas, disp);
	
	(* SIB byte *)
	code [pc, Emit.i] := USHORT (scl * 64 + idx MOD 8 * 8 + bas MOD 8);
	INC (Emit.i);
	
	Emit_disp (disp);
	Next_inst
END EmitRMs;

PROCEDURE EmitRM
(d : UBYTE; op : INTEGER; reg, rsize, rm : UBYTE; disp : INTEGER);
BEGIN
	IF ~ (rm IN {reg_R12, reg_SP}) THEN
		Emit.iflag := {}; Emit.i := 0;
		Emit_REX_prefix (rsize, reg, 0, rm, 0); Emit_16bit_prefix (rsize);
		Emit_opcodeR (op, rsize, d); Emit_ModRM (reg, rm, 0, disp);
		Emit_disp (disp);
		Next_inst
	ELSE
		EmitRMs (d, op, reg, rsize, rm, reg_SP, 0, disp)
	END
END EmitRM;

PROCEDURE EmitRR2 (op : INTEGER; reg, rsize, rm, rmsize : UBYTE);
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, reg, rm, 0, 0); Emit_opcodeR (op, rmsize, 1);
	
	(* ModR/M byte *)
	code [pc, Emit.i] := USHORT (0C0H + reg MOD 8 * 8 + rm MOD 8);
	INC (Emit.i);
	
	Next_inst
END EmitRR2;

PROCEDURE EmitRMs2
(op : INTEGER; reg, rsize, bas, idx, scl : UBYTE; disp : INTEGER; rmsize : UBYTE);
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, reg, 0, bas, idx); Emit_opcodeR (op, rmsize, 1);
	Emit_ModRM (reg, reg_SP, bas, disp);
	
	(* SIB byte *)
	code [pc, Emit.i] := USHORT (scl * 64 + idx MOD 8 * 8 + bas MOD 8);
	INC (Emit.i);
	
	Emit_disp (disp);
	Next_inst
END EmitRMs2;

PROCEDURE EmitRM2
(op : INTEGER; reg, rsize, rm : UBYTE; disp : INTEGER; rmsize : UBYTE);
BEGIN
	IF rm IN {reg_R12, reg_SP} THEN
		EmitRMs2 (op, reg, rsize, rm, reg_SP, 0, disp, rmsize)
	ELSE
		Emit.iflag := {}; Emit.i := 0;
		Emit_REX_prefix (rsize, reg, 0, rm, 0); Emit_opcodeR (op, rmsize, 1);
		Emit_ModRM (reg, rm, 0, disp); Emit_disp (disp);
		Next_inst
	END
END EmitRM2;

PROCEDURE MoveRI (rm, rsize : UBYTE; imm : LONGINT);
	VAR op : UBYTE;
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, 0, rm, 0, 0); Emit_16bit_prefix (rsize);
	
	op := USHORT (0B0H + rm);
	IF rsize > 1 THEN INC (op, w_bit_1byte) END;
	code [pc, Emit.i] := op;	(* Opcode byte *)
	INC (Emit.i);
	
	SYSTEM.PUT (SYSTEM.ADR (code [pc, Emit.i]), imm);
	INC (Emit.i, rsize);
	
	Next_inst
END MoveRI;

PROCEDURE EmitRRI (op : INTEGER; reg, rsize, rm : UBYTE; imm : LONGINT);
	VAR isize : UBYTE;
BEGIN
	Emit.iflag := {}; Emit.i := 0;
	Emit_REX_prefix (rsize, reg, rm, 0, 0); Emit_16bit_prefix (rsize);
	
	IF (rsize > 1) & (BITS(op) * BITS(w_bit) = {}) THEN INC (op, w_bit) END;
	
	isize := rsize;
	IF (isize > 1) & (imm >= MIN_INT8) & (imm <= MAX_INT8) THEN
		INC (op, s_bit); isize := 1
	ELSIF isize = 8 THEN isize := 4
	END;
	
	code [pc, Emit.i] := USHORT (op);	(* Opcode byte *)
	INC (Emit.i);
	
	(* ModR/M byte *)
	code [pc, Emit.i] := USHORT (0C0H + reg MOD 8 * 8 + rm MOD 8);
	INC (Emit.i);
	
	SYSTEM.PUT (SYSTEM.ADR (code [pc, Emit.i]), imm);
	INC (Emit.i, isize);
	
	Next_inst
END EmitRRI;

PROCEDURE EmitRI (op : INTEGER; rm, rsize : UBYTE; imm : LONGINT);
	VAR op3bit, isize : UBYTE;
BEGIN
	IF op # IMULi THEN
		Emit.iflag := {}; Emit.i := 0;
		Emit_REX_prefix (rsize, 0, rm, 0, 0); Emit_16bit_prefix (rsize);
		
		op3bit := USHORT (op DIV 256);
		op := op MOD 256;
		
		IF (rsize > 1) & (BITS(op) * BITS(w_bit) = {}) THEN INC (op, w_bit) END;
		
		isize := rsize;
		IF (isize > 1) & (imm >= MIN_INT8) & (imm <= MAX_INT8) THEN
			INC (op, s_bit); isize := 1
		ELSIF isize = 8 THEN isize := 4
		END;
		
		code [pc, Emit.i] := USHORT (op);	(* Opcode byte *)
		INC (Emit.i);
		
		(* ModR/M byte *)
		code [pc, Emit.i] := USHORT (0C0H + op3bit * 8 + rm MOD 8);
		INC (Emit.i);
		
		SYSTEM.PUT (SYSTEM.ADR (code [pc, Emit.i]), imm);
		INC (Emit.i, isize);
		
		Next_inst
	ELSE
		EmitRRI (IMULi, rm, rsize, rm, imm)
	END
END EmitRI;

(* -------------------------------------------------------------------------- *)
(* -------------------------------------------------------------------------- *)

PROCEDURE Reg_stack (offset : INTEGER) : UBYTE;
	VAR i : INTEGER;
BEGIN
	i := reg_stacks [crs].i;
	RETURN reg_stacks [crs].ord [i + offset]
END Reg_stack;

PROCEDURE Alloc_reg() : UBYTE;
	VAR i : INTEGER;
BEGIN
	IF reg_stacks [crs].i < reg_stacks [crs].n THEN INC (reg_stacks [crs].i)
	ELSE Mark ('Compiler limit: Register stack overflow')
	END;
	i := reg_stacks [crs].i - 1;
	RETURN reg_stacks [crs].ord [i]
END Alloc_reg;

PROCEDURE Free_reg;
BEGIN
	IF reg_stacks [crs].i > 0 THEN DEC (reg_stacks [crs].i)
	ELSE Mark ('Fault: Register stack overflow')
	END
END Free_reg;

PROCEDURE load* (VAR x : Item);
	VAR
		rsize : UBYTE;
BEGIN
	IF x.mode # Base.mode_reg THEN
		rsize := USHORT (x.type.size);
		IF x.mode = Base.class_const THEN
			x.r := Alloc_reg();
			IF (rsize = 8) & (x.a >= 0) & (x.a <= MAX_UINT32) THEN
				MoveRI (x.r, 4, x.a)
			ELSE MoveRI (x.r, rsize, x.a)
			END
		ELSIF x.mode = Base.mode_regI THEN
			EmitRM (1, MOVr, x.r, rsize, x.r, SHORT(x.a))
		END;
		x.mode := Base.mode_reg
	END
END load;

PROCEDURE Convert_BYTE* (VAR x : Item);
BEGIN
	IF x.type # Base.byte_type THEN (* Do nothing *)
	ELSE
		IF x.mode # Base.class_const THEN load (x);
			EmitRR2 (MOVZX, x.r, 4, x.r, 1)
		END;
		x.type := Base.int_type
	END
END Convert_BYTE;

PROCEDURE Small_const (VAR x : Item) : BOOLEAN;
BEGIN
	RETURN (x.mode = Base.class_const) & (x.a >= MIN_INT32) & (x.a <= MAX_INT32)
END Small_const;

PROCEDURE CommutativeOp (opR : UBYTE; opI : INTEGER; VAR x, y : Item);
BEGIN
	IF Small_const (y) THEN EmitRI (opI, x.r, 8, y.a)
	ELSIF ~ Small_const (x) THEN
		load (x); load (y); x.r := Reg_stack(-2);
		EmitRR (opR, x.r, 8, Reg_stack(-1)); Free_reg
	ELSE
		load (y); EmitRI (opI, y.r, 8, x.a);
		x.mode := Base.mode_reg; x.r := y.r
	END
END CommutativeOp;

PROCEDURE Subtract (VAR x, y : Item);
BEGIN
	IF x.mode # Base.class_const THEN
		IF Small_const (y) THEN EmitRI (SUBi, x.r, 8, y.a)
		ELSE load (y); EmitRR (SUBr, x.r, 8, y.r); Free_reg
		END
	ELSE
		load (x); load (y); x.r := Reg_stack(-2);
		EmitRR (SUBr, x.r, 8, Reg_stack(-1)); Free_reg
	END
END Subtract;

(* -------------------------------------------------------------------------- *)
(* -------------------------------------------------------------------------- *)

PROCEDURE Write_to_file* (from, n : INTEGER);
	VAR
		file : Sys.FileHandle;
		i, j : INTEGER;
BEGIN
	ASSERT (from < pc);
	ASSERT (from + n <= pc); 
	Sys.Rewrite (file, 'output.bin');
	
	FOR i := from TO from + n - 1 DO
		FOR j := 0 TO codeinfo [i].size - 1 DO
			Sys.Write_byte (file, code [i, j])
		END
	END;
	Sys.Close (file);
END Write_to_file;

PROCEDURE Init;
BEGIN
	pc := 0; ip := 0; crs := 0;
	reg_stacks [0].i := 0;
	reg_stacks [0].n := 11;
	reg_stacks [0].ord [0] := reg_A;
	reg_stacks [0].ord [1] := reg_C;
	reg_stacks [0].ord [2] := reg_D;
	reg_stacks [0].ord [3] := reg_R8;
	reg_stacks [0].ord [4] := reg_R9;
	reg_stacks [0].ord [5] := reg_R10;
	reg_stacks [0].ord [6] := reg_R11;
	reg_stacks [0].ord [7] := reg_R12;
	reg_stacks [0].ord [8] := reg_R13;
	reg_stacks [0].ord [9] := reg_R14;
	reg_stacks [0].ord [10] := reg_R15
END Init;

BEGIN
	Init;
	EmitRR2 (MOVZX, reg_A, 4, reg_A, 2);
	EmitRI (IMULi, reg_A, 8, 2);
	Write_to_file (0, pc)
END GeneratorWin64v2.