// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Export functions, strings, call graph and decompiled C for offline study.
// Usage (headless postScript): ExportAnalysis.java <output_dir>
// @category gd-game-01

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;

public class ExportAnalysis extends GhidraScript {

	private static final int CHUNK = 0x10000;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		Path out = Paths.get(args.length > 0 ? args[0] : "analysis");
		Files.createDirectories(out.resolve("decomp"));

		Listing listing = currentProgram.getListing();
		ReferenceManager refs = currentProgram.getReferenceManager();
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

		// strings.tsv: address, referencing functions, escaped value
		try (PrintWriter w = writer(out.resolve("strings.tsv"))) {
			w.println("address\treferenced_from\tvalue");
			for (Data d : listing.getDefinedData(true)) {
				if (!d.hasStringValue()) {
					continue;
				}
				Set<String> from = new TreeSet<>();
				for (Reference r : refs.getReferencesTo(d.getAddress())) {
					Function f = getFunctionContaining(r.getFromAddress());
					from.add(f != null ? f.getName() : r.getFromAddress().toString());
				}
				String v = String.valueOf(d.getValue())
					.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
				w.printf("%s\t%s\t%s%n", d.getAddress(), String.join(",", from), v);
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
		println("Exported " + funcs.size() + " functions, " + decompiled.size() + " decompiled, to " + out);
	}

	private static PrintWriter writer(Path p) throws IOException {
		return new PrintWriter(Files.newBufferedWriter(p, StandardCharsets.UTF_8));
	}
}
