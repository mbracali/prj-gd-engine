// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Prints the disassembly of one or more ranges, so call-site arguments can be
// read in the instructions (the decompiler sometimes drops them; kickoff rule 4).
// Usage (headless postScript): DumpAsm.java <addr>:<count> [<addr>:<count> ...]
//   addr in hex (0x prefix optional), count = number of instructions.
//   An argument "fn:<addr>" dumps the whole function containing addr.
// @category gd-game-04

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DumpAsm extends GhidraScript {
	@Override
	public void run() throws Exception {
		Listing listing = currentProgram.getListing();
		for (String arg : getScriptArgs()) {
			Address start;
			int count;
			if (arg.startsWith("fn:")) {
				Address a = toAddr(Long.parseUnsignedLong(arg.substring(3).replaceFirst("^0x", ""), 16));
				Function f = getFunctionContaining(a);
				if (f == null) {
					println("no function at " + a);
					continue;
				}
				start = f.getEntryPoint();
				count = (int) Math.min(100000, f.getBody().getNumAddresses() / 4);
				println("== " + f.getName() + " @ " + start + " (" + count + " instructions)");
			} else {
				String[] p = arg.split(":");
				start = toAddr(Long.parseUnsignedLong(p[0].replaceFirst("^0x", ""), 16));
				count = p.length > 1 ? Integer.parseInt(p[1]) : 32;
				println("== " + start + " x" + count);
			}
			Address a = start;
			for (int i = 0; i < count && a != null; i++) {
				Instruction ins = listing.getInstructionAt(a);
				StringBuilder sb = new StringBuilder();
				sb.append(a.toString()).append("  ");
				if (ins == null) {
					int w = getInt(a);
					sb.append(String.format(".word 0x%08x", w));
					a = a.add(4);
				} else {
					sb.append(ins.toString());
					for (Reference r : ins.getReferencesFrom()) {
						Symbol s = getSymbolAt(r.getToAddress());
						sb.append("  ; -> ").append(r.getToAddress());
						if (s != null) {
							sb.append(" ").append(s.getName());
						}
					}
					a = ins.getMaxAddress().add(1);
				}
				println(sb.toString());
			}
		}
	}
}
