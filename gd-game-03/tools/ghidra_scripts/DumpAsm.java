// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Print the disassembly at one or more addresses, for reading call sites where the
// decompiler drops arguments (kickoff rule 4).
// Usage (headless postScript, -readOnly -noanalysis): DumpAsm.java <addr>:<count> [<addr>:<count> ...]
// <addr> is hex (with or without 0x); <count> is the number of instructions (default 40).
// A function name may be given instead of an address.
// @category gd-game-03

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DumpAsm extends GhidraScript {

	@Override
	public void run() throws Exception {
		Listing listing = currentProgram.getListing();
		for (String arg : getScriptArgs()) {
			String[] p = arg.split(":");
			int count = p.length > 1 ? Integer.parseInt(p[1]) : 40;
			Address a = resolve(p[0]);
			if (a == null) {
				println("DumpAsm: cannot resolve " + p[0]);
				continue;
			}
			Function f = getFunctionContaining(a);
			println("==== " + a + (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : "") + " ====");
			Instruction ins = listing.getInstructionAt(a);
			if (ins == null) {
				ins = listing.getInstructionAfter(a);
			}
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < count && ins != null; i++, ins = ins.getNext()) {
				sb.append(ins.getAddress()).append("  ").append(ins.toString());
				for (Reference r : ins.getReferencesFrom()) {
					Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(r.getToAddress());
					sb.append("  ; -> ").append(r.getToAddress()).append(s != null ? " " + s.getName() : "");
				}
				sb.append('\n');
			}
			println(sb.toString());
		}
	}

	private Address resolve(String s) {
		try {
			return toAddr(Long.parseLong(s.startsWith("0x") ? s.substring(2) : s, 16));
		}
		catch (NumberFormatException e) {
			for (Function f : currentProgram.getListing().getFunctions(true)) {
				if (f.getName().equals(s)) {
					return f.getEntryPoint();
				}
			}
			return null;
		}
	}
}
