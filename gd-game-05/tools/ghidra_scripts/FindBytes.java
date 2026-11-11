// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Finds byte patterns in the loaded program's memory and lists the
// references to each hit (and the functions they are in).
// Usage (headless postScript): FindBytes.java <hex bytes | "text:..."> ...
// @category gd-game-04

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class FindBytes extends GhidraScript {
	@Override
	public void run() throws Exception {
		for (String arg : getScriptArgs()) {
			byte[] pat;
			if (arg.startsWith("text:")) {
				pat = arg.substring(5).getBytes("ISO-8859-1");
			} else {
				String h = arg.replace(" ", "");
				pat = new byte[h.length() / 2];
				for (int i = 0; i < pat.length; i++) {
					pat[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
				}
			}
			println("== " + arg);
			Address a = currentProgram.getMinAddress();
			int hits = 0;
			while (a != null && hits < 50) {
				a = currentProgram.getMemory().findBytes(a, pat, null, true, monitor);
				if (a == null) {
					break;
				}
				StringBuilder sb = new StringBuilder("hit " + a);
				for (Reference r : getReferencesTo(a)) {
					Function f = getFunctionContaining(r.getFromAddress());
					sb.append("  ref " + r.getFromAddress() + (f != null ? " in " + f.getName() : ""));
				}
				println(sb.toString());
				hits++;
				a = a.add(1);
			}
		}
	}
}
