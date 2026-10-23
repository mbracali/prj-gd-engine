// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Create functions and names from analysis/vtables.tsv (see tools/vtables.py).
// Methods are named <Class>_v<slot> for a class's first vtable and
// <Class>_t<table>_v<slot> for its other (base subobject) vtables. A method
// shared by several vtables is named after the owner with the fewest slots,
// the likeliest base class; a comment lists every owner.
// Usage (headless postScript): ApplyVtables.java <vtables.tsv>
// @category gd-game-01

import java.nio.file.*;
import java.util.*;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class ApplyVtables extends GhidraScript {

	@Override
	public void run() throws Exception {
		List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
		Map<String, String> vptrs = new LinkedHashMap<>();   // "Class:table" -> vptr
		Map<String, Integer> slotsOf = new HashMap<>();      // class -> total slots
		Map<Long, List<String>> owners = new TreeMap<>();    // method -> "Class:table:slot"
		for (String line : lines.subList(1, lines.size())) {
			String[] f = line.split("\t");
			vptrs.put(f[0] + ":" + f[2], f[1]);
			slotsOf.merge(f[0], 1, Integer::sum);
			owners.computeIfAbsent(Long.parseLong(f[4], 16), k -> new ArrayList<>()).add(f[0] + ":" + f[2] + ":" + f[3]);
		}

		int created = 0, named = 0;
		for (Map.Entry<String, String> e : vptrs.entrySet()) {
			String[] ct = e.getKey().split(":");
			Address vptr = toAddr(Long.parseLong(e.getValue(), 16));
			createLabel(vptr, ct[0] + (ct[1].equals("0") ? "_vtable" : "_vtable" + ct[1]), true, SourceType.ANALYSIS);
			Function ti = getFunctionAt(toAddr(getInt(vptr.add(4)) & 0xffffffffL));
			if (ti != null && ti.getName().startsWith("FUN_")) {
				ti.setName(ct[0] + "_typeinfo", SourceType.ANALYSIS);
			}
		}
		for (Map.Entry<Long, List<String>> e : owners.entrySet()) {
			Address a = toAddr(e.getKey());
			Function fn = getFunctionAt(a);
			if (fn == null) {
				disassemble(a);
				if (new CreateFunctionCmd(a).applyTo(currentProgram, monitor)) {
					created++;
				}
				fn = getFunctionAt(a);
				if (fn == null) {
					continue;
				}
			}
			List<String> own = e.getValue();
			String base = Collections.min(own, Comparator.comparingInt(o -> slotsOf.get(o.split(":")[0])));
			String[] cts = base.split(":");
			if (fn.getName().startsWith("FUN_")) {
				String name = cts[1].equals("0") ? cts[0] + "_v" + cts[2] : cts[0] + "_t" + cts[1] + "_v" + cts[2];
				fn.setName(name, SourceType.ANALYSIS);
				named++;
			}
			if (own.size() > 1) {
				fn.setComment("vtable slot of: " + String.join(", ", own));
			}
		}
		analyzeChanges(currentProgram);
		println("Created " + created + " functions, named " + named + " methods for " + vptrs.size() + " vtables");
	}
}
