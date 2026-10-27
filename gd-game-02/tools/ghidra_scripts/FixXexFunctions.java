// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Repair the function layout of an imported Xbox 360 XEX (XEXLoaderWV) and save it.
//
// Ghidra's first pass treats the compiler's register save/restore helpers
// (__savegprlr_NN, __restgprlr_NN, __savefpr_NN, __restfpr_NN, __savevmx_NN,
// __restvmx_NN) as non-returning, so every function that calls one in its
// prologue is cut to a few bytes. This script:
//   1. finds the helper chains by their instruction patterns, creates one
//      function per entry (named, returning, inlined into callers so the
//      decompiler sees straight-line code and keeps r3 across the restore tail);
//   2. clears the no-return flag on the other functions that really return;
//   3. reads .pdata from the program's memory ({u32 start, u32 flags},
//      length = ((flags >> 8) & 0x3fffff) * 4), clears and re-disassembles each
//      range and (re)creates the function with exactly that body (leaf
//      functions have no .pdata entry and are left to Ghidra);
//   4. re-runs auto-analysis (no-return discovery off) so references, stack
//      frames and switch tables are rebuilt, then re-applies the .pdata bodies;
//   5. pairs "lis rX,hi" with the following addi/load/store on rX, adds the
//      data references Ghidra missed and defines the C strings they point at.
// The program is saved by the headless analyzer when the script ends.
// Usage (headless postScript, without -readOnly): FixXexFunctions.java [noinline] [noanalysis]
// @category gd-game-02

import java.util.*;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class FixXexFunctions extends GhidraScript {

	private static final int BLR = 0x4e800020;

	/** One save/restore helper family: anchor = first instruction of entry 0. */
	private record Chain(String prefix, int anchor, int anchor2Op, int unit, int first, int count) {
	}

	// gpr: std/ld rN,-0x98+8*(N-14)(r1); fpr: stfd/lfd fN,-0x90+8*(N-14)(r12);
	// vmx: li r11,off + stvx/lvx vN,r11,r12 (opcode 31), vmx128 forms (opcode 4).
	private static final Chain[] CHAINS = {
		new Chain("__savegprlr_", 0xf9c1ff68, -1, 4, 14, 18),
		new Chain("__restgprlr_", 0xe9c1ff68, -1, 4, 14, 18),
		new Chain("__savefpr_", 0xd9ccff70, -1, 4, 14, 18),
		new Chain("__restfpr_", 0xc9ccff70, -1, 4, 14, 18),
		new Chain("vmx_", 0x3960fee0, 31, 8, 14, 18),
		new Chain("vmx_", 0x3960fc00, 4, 8, 64, 64),
	};

	private Memory mem;
	private Listing listing;
	private FunctionManager fm;
	private AddressSet helperSet = new AddressSet();
	private Set<Address> helperEntries = new HashSet<>();

	@Override
	public void run() throws Exception {
		Set<String> opts = new HashSet<>(Arrays.asList(getScriptArgs()));
		boolean inline = !opts.contains("noinline");
		mem = currentProgram.getMemory();
		listing = currentProgram.getListing();
		fm = currentProgram.getFunctionManager();

		int helpers = fixHelpers(inline);
		println("save/restore helpers: " + helpers + " entries");
		println("no-return cleared (before .pdata): " + clearFalseNoReturn());

		List<long[]> pdata = readPdata();
		println(".pdata entries: " + pdata.size());
		int[] st = applyPdata(pdata, true);
		println(".pdata functions: created " + st[0] + ", resized " + st[1] + ", inner removed " +
			st[2] + ", skipped " + st[3]);
		println("no-return cleared (after .pdata): " + clearFalseNoReturn());

		if (!opts.contains("noanalysis")) {
			setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "false");
			println("running auto-analysis...");
			analyzeAll(currentProgram);
			st = applyPdata(pdata, false);
			println("after analysis: bodies re-applied " + st[1] + ", inner removed " + st[2]);
			println("no-return cleared (after analysis): " + clearFalseNoReturn());
		}
		int holes = fillHoles(pdata);
		println("undefined words left inside .pdata ranges: " + holes);

		int[] r = addHiLoReferences();
		println("lis/lo pairs: " + r[0] + " references added, " + r[1] + " strings defined");

		int nr = 0;
		for (Function f : fm.getFunctions(true)) {
			if (f.hasNoReturn()) {
				nr++;
				println("  still no-return: " + f.getName() + " @ " + f.getEntryPoint() + " size " +
					f.getBody().getNumAddresses());
			}
		}
		println("functions: " + fm.getFunctionCount() + ", no-return: " + nr);
	}

	// ---- 1. save/restore helpers ----

	private int fixHelpers(boolean inline) throws Exception {
		int made = 0;
		AddressSetView exec = executableSet();
		for (Chain c : CHAINS) {
			byte[] pat = be(c.anchor);
			Address from = exec.getMinAddress();
			while (from != null) {
				Address a = mem.findBytes(from, exec.getMaxAddress(), pat, null, true, monitor);
				if (a == null) {
					break;
				}
				from = a.add(4);
				if (!exec.contains(a) || a.getOffset() % 4 != 0) {
					continue;
				}
				Address end = matchChain(c, a);
				if (end == null) {
					continue;
				}
				String prefix = c.prefix;
				if (prefix.equals("vmx_")) {
					prefix = (mem.getInt(a.add(4)) & 0x100) != 0 ? "__savevmx_" : "__restvmx_";
				}
				made += createChain(prefix, c, a, end, inline);
			}
		}
		return made;
	}

	/** Returns the blr closing the chain at a, or null if the bytes don't match. */
	private Address matchChain(Chain c, Address a) throws MemoryAccessException {
		int op = c.anchor >>> 26;
		for (int k = 0; k < c.count; k++) {
			Address u = a.add((long) k * c.unit);
			int w = mem.getInt(u);
			if (w >>> 26 != op) {
				return null;
			}
			if (c.unit == 8) {
				// li r11,off with off stepping by 16, then a vector store/load
				if ((w & 0xffff0000) != 0x39600000 || (short) w != (short) c.anchor + 16 * k) {
					return null;
				}
				if (mem.getInt(u.add(4)) >>> 26 != c.anchor2Op) {
					return null;
				}
			}
			else if ((short) w != (short) c.anchor + 8 * k || ((w >>> 21) & 31) != c.first + k) {
				return null;
			}
		}
		Address p = a.add((long) c.count * c.unit);
		for (int i = 0; i < 4; i++, p = p.add(4)) {
			if (mem.getInt(p) == BLR) {
				return p;
			}
		}
		return null;
	}

	private int createChain(String prefix, Chain c, Address a, Address blr, boolean inline)
			throws Exception {
		AddressSet all = new AddressSet(a, blr.add(3));
		for (Function f : toList(fm.getFunctionsOverlapping(all))) {
			fm.removeFunction(f.getEntryPoint());
		}
		clearListing(all);
		disassembleExact(all);
		for (int k = 0; k < c.count; k++) {
			Address s = a.add((long) k * c.unit);
			Address e = k + 1 < c.count ? s.add(c.unit - 1) : blr.add(3);
			String name = prefix + (c.first + k);
			Function f = fm.createFunction(name, s, new AddressSet(s, e), SourceType.USER_DEFINED);
			f.setNoReturn(false);
			f.setInline(inline);
			helperEntries.add(s);
		}
		helperSet.add(all);
		println(String.format("  %s%d..%d at %s", prefix, c.first, c.first + c.count - 1, a));
		return c.count;
	}

	// ---- 2. no-return flags ----

	/** Clears no-return on functions whose code reaches a blr or tail-branches to a returning function. */
	private int clearFalseNoReturn() {
		int cleared = 0;
		boolean changed = true;
		while (changed) {
			changed = false;
			for (Function f : fm.getFunctions(true)) {
				if (f.hasNoReturn() && returns(f)) {
					f.setNoReturn(false);
					cleared++;
					changed = true;
				}
			}
		}
		return cleared;
	}

	private boolean returns(Function f) {
		Address p = f.getEntryPoint();
		// straight-line code from the entry up to a return or a tail branch
		for (int i = 0; i < 64; i++) {
			Instruction ins = listing.getInstructionAt(p);
			if (ins == null) {
				break;
			}
			if (isReturn(ins)) {
				return true;
			}
			FlowType ft = ins.getFlowType();
			if (ft.isJump() && !ft.isConditional()) {
				Address[] t = ins.getFlows();
				if (t.length != 1) {
					break;
				}
				Function g = fm.getFunctionAt(t[0]);
				return g != null && g != f && !g.hasNoReturn();
			}
			if (ft.isTerminal() || ins.getFallThrough() == null) {
				break;
			}
			if (ft.isCall()) {
				Address[] t = ins.getFlows();
				Function g = t.length == 1 ? fm.getFunctionAt(t[0]) : null;
				if (g != null && g.hasNoReturn()) {
					break;
				}
			}
			p = ins.getFallThrough();
		}
		for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
			if (isReturn(ins)) {
				return true;
			}
		}
		return false;
	}

	private static boolean isReturn(Instruction ins) {
		return ins.getMnemonicString().matches("b[a-z]*lr");
	}

	// ---- 3. .pdata ----

	private List<long[]> readPdata() throws MemoryAccessException {
		MemoryBlock b = mem.getBlock(".pdata");
		if (b == null) {
			throw new IllegalStateException("no .pdata block");
		}
		List<long[]> out = new ArrayList<>();
		AddressSetView exec = executableSet();
		for (Address p = b.getStart(); p.add(7).compareTo(b.getEnd()) <= 0; p = p.add(8)) {
			long start = mem.getInt(p) & 0xffffffffL;
			int flags = mem.getInt(p.add(4));
			long len = ((flags >>> 8) & 0x3fffffL) * 4;
			if (start == 0 || len == 0) {
				continue;
			}
			Address s = toAddr(start);
			if (!exec.contains(s) || !exec.contains(s.add(len - 1))) {
				continue;
			}
			out.add(new long[] { start, len });
		}
		out.sort(Comparator.comparingLong(e -> e[0]));
		return out;
	}

	/** Returns {created, resized, inner functions removed, skipped}. */
	private int[] applyPdata(List<long[]> pdata, boolean redisassemble) throws Exception {
		int[] st = new int[4];
		monitor.initialize(pdata.size());
		for (long[] e : pdata) {
			monitor.checkCancelled();
			monitor.incrementProgress(1);
			Address s = toAddr(e[0]);
			AddressSet body = new AddressSet(s, s.add(e[1] - 1));
			if (body.intersects(helperSet)) {
				st[3]++;
				continue;
			}
			Function f = fm.getFunctionAt(s);
			if (!redisassemble && f != null && f.getBody().equals(body)) {
				continue;
			}
			for (Function g : toList(fm.getFunctionsOverlapping(body))) {
				if (g.getEntryPoint().equals(s)) {
					continue;
				}
				if (body.contains(g.getEntryPoint())) {
					fm.removeFunction(g.getEntryPoint());
					st[2]++;
				}
				else {
					g.setBody(g.getBody().subtract(body));
				}
			}
			if (redisassemble) {
				clearListing(body);
				DisassembleCommand cmd = new DisassembleCommand(s, body, true);
				cmd.enableCodeAnalysis(false);
				cmd.applyTo(currentProgram, monitor);
			}
			try {
				if (f == null) {
					fm.createFunction(null, s, body, SourceType.DEFAULT);
					st[0]++;
				}
				else if (!f.getBody().equals(body)) {
					f.setBody(body);
					st[1]++;
				}
			}
			catch (Exception ex) {
				printerr("  " + s + ": " + ex.getMessage());
				st[3]++;
			}
		}
		return st;
	}

	/**
	 * Disassembles code left undefined inside .pdata ranges (reached only through
	 * unresolved computed branches); words that look like pointers into the image
	 * (jump tables) are left as data. Returns the number of undefined words left.
	 */
	private int fillHoles(List<long[]> pdata) throws Exception {
		AddressSet seeds = new AddressSet();
		AddressSetView image = mem.getLoadedAndInitializedAddressSet();
		for (long[] e : pdata) {
			Address s = toAddr(e[0]);
			AddressSet body = new AddressSet(s, s.add(e[1] - 1));
			if (body.intersects(helperSet)) {
				continue;
			}
			for (Address p = s; body.contains(p); p = p.add(4)) {
				if (listing.isUndefined(p, p.add(3))) {
					int w = mem.getInt(p);
					if (w != 0 && !image.contains(toAddr(w & 0xffffffffL))) {
						seeds.add(p);
					}
				}
			}
		}
		for (AddressRange r : seeds.getAddressRanges()) {
			Function f = fm.getFunctionContaining(r.getMinAddress());
			AddressSetView limit = f != null ? f.getBody() : new AddressSet(r);
			DisassembleCommand cmd = new DisassembleCommand(r.getMinAddress(), limit, true);
			cmd.enableCodeAnalysis(false);
			cmd.applyTo(currentProgram, monitor);
		}
		int left = 0;
		for (long[] e : pdata) {
			Address s = toAddr(e[0]);
			for (long o = 0; o < e[1]; o += 4) {
				if (listing.isUndefined(s.add(o), s.add(o + 3))) {
					left++;
				}
			}
		}
		return left;
	}

	// ---- 5. lis/lo pairs ----

	/** Returns {references added, strings defined}. */
	private int[] addHiLoReferences() throws Exception {
		ReferenceManager rm = currentProgram.getReferenceManager();
		AddressSetView image = mem.getLoadedAndInitializedAddressSet();
		AddressSetView exec = executableSet();
		int refs = 0, strings = 0;
		for (Function f : fm.getFunctions(true)) {
			monitor.checkCancelled();
			Map<Register, Long> hi = new HashMap<>();
			for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
				String m = ins.getMnemonicString();
				Object[] o0 = ins.getOpObjects(0);
				if (m.equals("lis") && ins.getNumOperands() == 2 && o0.length == 1 &&
					o0[0] instanceof Register r) {
					Object[] o1 = ins.getOpObjects(1);
					if (o1.length == 1 && o1[0] instanceof Scalar sc) {
						hi.put(r, (sc.getUnsignedValue() & 0xffff) << 16);
						continue;
					}
				}
				long[] t = loTarget(ins, hi);
				if (t != null) {
					Address to = toAddr(t[0]);
					if (image.contains(to) && !hasRef(ins, to)) {
						RefType type = m.startsWith("st") ? RefType.WRITE
							: m.startsWith("l") ? RefType.READ : RefType.DATA;
						rm.addMemoryReference(ins.getAddress(), to, type, SourceType.ANALYSIS,
							(int) t[1]);
						refs++;
					}
					if (m.equals("addi") && !exec.contains(to) && defineString(to)) {
						strings++;
					}
				}
				for (Object o : ins.getResultObjects()) {
					if (o instanceof Register r) {
						hi.remove(r);
						hi.keySet().removeIf(k -> k.contains(r) || r.contains(k));
					}
				}
			}
		}
		return new int[] { refs, strings };
	}

	/** {target, operand index} if ins uses a register loaded by lis plus a 16-bit offset. */
	static long[] loTarget(Instruction ins, Map<Register, Long> hi) {
		String m = ins.getMnemonicString();
		if (m.startsWith("b") || m.startsWith("cmp") || m.startsWith("tw") || m.startsWith("td")) {
			return null;
		}
		int n = ins.getNumOperands();
		for (int k = 1; k < n; k++) {
			Register r = null;
			Scalar s = null;
			for (Object o : ins.getOpObjects(k)) {
				if (o instanceof Register x) {
					r = x;
				}
				else if (o instanceof Scalar x) {
					s = x;
				}
			}
			if (r == null || !hi.containsKey(r)) {
				continue;
			}
			int opIndex = k;
			if (s == null && k + 1 < n) {
				for (Object o : ins.getOpObjects(k + 1)) {
					if (o instanceof Scalar x) {
						s = x;
						opIndex = k + 1;
					}
				}
			}
			if (s == null) {
				return null;
			}
			long base = hi.get(r);
			long target;
			if (m.startsWith("ori")) {
				target = base | (s.getUnsignedValue() & 0xffff);
			}
			else if (m.startsWith("subi")) {
				target = base - s.getUnsignedValue();
			}
			else if (m.equals("addi") || m.startsWith("l") || m.startsWith("st")) {
				target = base + (short) s.getValue();
			}
			else {
				return null;
			}
			return new long[] { target & 0xffffffffL, opIndex };
		}
		return null;
	}

	private static boolean hasRef(Instruction ins, Address to) {
		for (Reference r : ins.getReferencesFrom()) {
			if (r.getToAddress().equals(to)) {
				return true;
			}
		}
		return false;
	}

	/** Defines a NUL-terminated printable string (2+ chars) at an undefined address. */
	private boolean defineString(Address a) throws Exception {
		if (listing.getDefinedDataContaining(a) != null || listing.getInstructionContaining(a) != null) {
			return false;
		}
		int n = printableLength(mem, a, 4096);
		if (n < 2 || !listing.isUndefined(a, a.add(n))) {
			return false;
		}
		createAsciiString(a);
		return true;
	}

	/** Length of the printable ASCII run at a if it ends with NUL, else -1. */
	static int printableLength(Memory mem, Address a, int max) {
		try {
			for (int i = 0; i < max; i++) {
				int b = mem.getByte(a.add(i)) & 0xff;
				if (b == 0) {
					return i;
				}
				if ((b < 0x20 || b > 0x7e) && b != '\t' && b != '\n' && b != '\r') {
					return -1;
				}
			}
		}
		catch (Exception e) {
			// ran off the block
		}
		return -1;
	}

	// ---- utilities ----

	private AddressSetView executableSet() {
		AddressSet s = new AddressSet();
		for (MemoryBlock b : mem.getBlocks()) {
			if (b.isExecute() && b.isInitialized()) {
				s.add(b.getStart(), b.getEnd());
			}
		}
		return s;
	}

	private void disassembleExact(AddressSetView set) {
		DisassembleCommand cmd = new DisassembleCommand(set, set, true);
		cmd.enableCodeAnalysis(false);
		cmd.applyTo(currentProgram, monitor);
	}

	private static List<Function> toList(Iterator<Function> it) {
		List<Function> l = new ArrayList<>();
		it.forEachRemaining(l::add);
		return l;
	}

	private static byte[] be(int w) {
		return new byte[] { (byte) (w >>> 24), (byte) (w >>> 16), (byte) (w >>> 8), (byte) w };
	}
}
