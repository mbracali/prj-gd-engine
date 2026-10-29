// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Export functions, strings, data references, call graph and decompiled C of a
// PS2 Emotion Engine ELF (MIPS R5900) for offline study. Ported from gd-game-02's
// ExportAnalysisXex: the linear scan pairs "lui rX,hi" with the following
// addiu/ori/load/store on rX (delay-slot "_" prefixes stripped).
// Outputs: functions.tsv, strings.tsv (with referencing functions), refs.tsv, decomp/*.c
// Usage (headless postScript): ExportAnalysisElf.java <output_dir>
// @category gd-game-03

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;

public class ExportAnalysisElf extends GhidraScript {

	private static final int CHUNK = 0x10000;
	private static final Set<String> LOADS = Set.of("lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "lq",
		"lwc1", "ldc1", "lwl", "lwr", "ldl", "ldr", "lqc2");
	private static final Set<String> STORES = Set.of("sb", "sh", "sw", "sd", "sq", "swc1", "sdc1", "swl",
		"swr", "sdl", "sdr", "sqc2");

	/** One data reference: target, instruction, containing function, kind. */
	private record Ref(long to, Address from, String func, String kind, String ins) {
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			throw new IllegalArgumentException("usage: ExportAnalysisElf.java <output_dir>");
		}
		Path out = Paths.get(args[0]);
		Files.createDirectories(out.resolve("decomp"));

		Listing listing = currentProgram.getListing();
		Memory mem = currentProgram.getMemory();
		List<Function> funcs = new ArrayList<>();
		listing.getFunctions(true).forEach(funcs::add);

		// functions.tsv: entry, name, size, caller count, callee names
		try (PrintWriter w = writer(out.resolve("functions.tsv"))) {
			w.println("entry\tname\tsize\tcallers\tcallees");
			for (Function f : funcs) {
				Set<Function> callers = f.getCallingFunctions(monitor);
				StringJoiner callees = new StringJoiner(",");
				for (Function c : f.getCalledFunctions(monitor)) {
					callees.add(c.getName());
				}
				w.printf("%s\t%s\t%d\t%d\t%s%n", f.getEntryPoint(), f.getName(),
					f.getBody().getNumAddresses(), callers.size(), callees);
			}
		}

		// data references: Ghidra's own plus the lui/lo scan, one per (from, to)
		Map<String, Ref> refMap = new LinkedHashMap<>();
		Set<String> ghidraKeys = new HashSet<>();
		Set<String> scanKeys = new HashSet<>();
		AddressSetView image = mem.getLoadedAndInitializedAddressSet();
		for (Function f : funcs) {
			monitor.checkCancelled();
			Map<Register, Long> hi = new HashMap<>();
			for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
				for (Reference r : ins.getReferencesFrom()) {
					if (r.isMemoryReference() && !r.getReferenceType().isFlow()) {
						long to = r.getToAddress().getOffset();
						String k = key(ins.getAddress(), to);
						ghidraKeys.add(k);
						refMap.putIfAbsent(k, new Ref(to, ins.getAddress(), f.getName(),
							kind(r.getReferenceType()), ins.toString()));
					}
				}
				String m = mn(ins);
				Object[] o0 = ins.getOpObjects(0);
				if (m.equals("lui") && ins.getNumOperands() == 2 && o0.length == 1 &&
					o0[0] instanceof Register r) {
					Object[] o1 = ins.getOpObjects(1);
					if (o1.length == 1 && o1[0] instanceof Scalar sc) {
						hi.put(r, (sc.getUnsignedValue() & 0xffff) << 16);
						continue;
					}
				}
				Long t = loTarget(ins, hi);
				if (t != null && image.contains(toAddr(t))) {
					String k = key(ins.getAddress(), t);
					scanKeys.add(k);
					String kind = STORES.contains(m) ? "WRITE" : LOADS.contains(m) ? "READ" : "ADDR";
					refMap.putIfAbsent(k, new Ref(t, ins.getAddress(), f.getName(), kind, ins.toString()));
				}
				for (Object o : ins.getResultObjects()) {
					if (o instanceof Register r) {
						hi.keySet().removeIf(x -> x.equals(r) || x.contains(r) || r.contains(x));
					}
				}
			}
		}
		Map<Long, Set<String>> refsTo = new HashMap<>();
		for (Ref r : refMap.values()) {
			refsTo.computeIfAbsent(r.to, k -> new TreeSet<>()).add(r.func);
		}

		// refs.tsv: target, label at target, instruction address, function, kind,
		// origin (ghidra = Ghidra's reference only, scan = lui/lo scan only, both), instruction
		SymbolTable symbols = currentProgram.getSymbolTable();
		try (PrintWriter w = writer(out.resolve("refs.tsv"))) {
			w.println("to\tto_label\tfrom\tfunction\tkind\torigin\tinstruction");
			List<Ref> all = new ArrayList<>(refMap.values());
			all.sort(Comparator.comparingLong((Ref r) -> r.to).thenComparing(r -> r.from));
			for (Ref r : all) {
				Symbol s = symbols.getPrimarySymbol(toAddr(r.to));
				String k = key(r.from, r.to);
				String origin = !scanKeys.contains(k) ? "ghidra" : ghidraKeys.contains(k) ? "both" : "scan";
				w.printf("%08x\t%s\t%s\t%s\t%s\t%s\t%s%n", r.to, s != null ? s.getName() : "",
					r.from, r.func, r.kind, origin, r.ins);
			}
		}

		// strings.tsv: address, referencing functions, escaped value; defined strings plus
		// NUL-terminated text that only the scan points at (e.g. names under 5 chars)
		TreeMap<Long, String[]> strings = new TreeMap<>();
		for (Data d : listing.getDefinedData(true)) {
			if (d.hasStringValue()) {
				long a = d.getAddress().getOffset();
				strings.put(a, new String[] { join(refsTo.get(a)), String.valueOf(d.getValue()) });
			}
		}
		int extra = 0;
		for (Map.Entry<Long, Set<String>> e : refsTo.entrySet()) {
			Address a = toAddr(e.getKey());
			MemoryBlock b = mem.getBlock(a);
			if (strings.containsKey(e.getKey()) || b == null || b.isExecute() ||
				listing.getDefinedDataContaining(a) != null) {
				continue;
			}
			String v = cString(mem, a, 4096);
			if (v != null && v.length() >= 2) {
				strings.put(e.getKey(), new String[] { join(e.getValue()), v });
				extra++;
			}
		}
		int withRefs = 0;
		try (PrintWriter w = writer(out.resolve("strings.tsv"))) {
			w.println("address\treferenced_from\tvalue");
			for (Map.Entry<Long, String[]> e : strings.entrySet()) {
				String v = e.getValue()[1]
					.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
				w.printf("%08x\t%s\t%s%n", e.getKey(), e.getValue()[0], v);
				if (!e.getValue()[0].isEmpty()) {
					withRefs++;
				}
			}
		}

		// decomp/<chunk>.c: pseudocode grouped by 64 KiB address range
		DecompileConfigurer configurer = decompiler -> {
			DecompileOptions options = new DecompileOptions();
			options.grabFromProgram(currentProgram);
			decompiler.setOptions(options);
			decompiler.toggleCCode(true);
			decompiler.toggleSyntaxTree(false);
			decompiler.setSimplificationStyle("decompile");
		};
		DecompilerCallback<String[]> callback = new DecompilerCallback<>(currentProgram, configurer) {
			@Override
			public String[] process(DecompileResults results, TaskMonitor m) {
				Function f = results.getFunction();
				String body = results.getDecompiledFunction() != null
					? results.getDecompiledFunction().getC()
					: "/* decompile failed: " + results.getErrorMessage() + " */\n";
				return new String[] { f.getEntryPoint().toString(), f.getName(), body };
			}
		};
		callback.setTimeout(120);
		List<String[]> decompiled;
		try {
			decompiled = ParallelDecompiler.decompileFunctions(callback, funcs, monitor);
		}
		finally {
			callback.dispose();
		}

		decompiled.sort(Comparator.comparingLong(r -> Long.parseLong(r[0], 16)));
		Map<Long, StringBuilder> chunks = new TreeMap<>();
		for (String[] r : decompiled) {
			long addr = Long.parseLong(r[0], 16);
			chunks.computeIfAbsent(addr / CHUNK * CHUNK, k -> new StringBuilder())
				.append("// ==== FUNCTION ").append(r[1]).append(" @ 0x").append(r[0]).append(" ====\n")
				.append(r[2]).append("\n");
		}
		for (Map.Entry<Long, StringBuilder> e : chunks.entrySet()) {
			Files.writeString(out.resolve("decomp").resolve(String.format("%08x.c", e.getKey())),
				e.getValue(), StandardCharsets.UTF_8);
		}
		println("Exported " + funcs.size() + " functions, " + decompiled.size() + " decompiled, " +
			refMap.size() + " data refs (" + scanKeys.size() + " from the lui/lo scan), " + strings.size() +
			" strings (" + extra + " found by the scan, " + withRefs + " referenced), to " + out);
	}

	/** Target of ins if it adds a 16-bit offset to a register loaded by lui, else null. */
	private static Long loTarget(Instruction ins, Map<Register, Long> hi) {
		String m = mn(ins);
		boolean add = m.equals("addiu") || m.equals("addi") || m.equals("ori");
		if (!add && !LOADS.contains(m) && !STORES.contains(m)) {
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
			if (s == null && k + 1 < n) {
				for (Object o : ins.getOpObjects(k + 1)) {
					if (o instanceof Scalar x) {
						s = x;
					}
				}
			}
			if (s == null) {
				return null;
			}
			long base = hi.get(r);
			long t = m.equals("ori") ? base | (s.getUnsignedValue() & 0xffff)
				: base + (short) s.getValue();
			return t & 0xffffffffL;
		}
		return null;
	}

	/** NUL-terminated printable ASCII at a, or null. */
	private static String cString(Memory mem, Address a, int max) {
		StringBuilder sb = new StringBuilder();
		try {
			for (int i = 0; i < max; i++) {
				int b = mem.getByte(a.add(i)) & 0xff;
				if (b == 0) {
					return sb.toString();
				}
				if ((b < 0x20 || b > 0x7e) && b != '\t' && b != '\n' && b != '\r') {
					return null;
				}
				sb.append((char) b);
			}
		}
		catch (Exception e) {
			// ran off the block
		}
		return null;
	}

	/** Mnemonic without Ghidra's delay-slot "_" prefix, lower case. */
	private static String mn(Instruction ins) {
		String m = ins.getMnemonicString().toLowerCase();
		return m.startsWith("_") ? m.substring(1) : m;
	}

	private static String kind(RefType t) {
		return t.isWrite() ? "WRITE" : t.isRead() ? "READ" : "ADDR";
	}

	private static String key(Address from, long to) {
		return from + ">" + Long.toHexString(to);
	}

	private static String join(Set<String> s) {
		return s == null ? "" : String.join(",", s);
	}

	private static PrintWriter writer(Path p) throws IOException {
		return new PrintWriter(Files.newBufferedWriter(p, StandardCharsets.UTF_8));
	}
}
