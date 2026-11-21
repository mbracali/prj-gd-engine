// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Name the PSP import stubs (.sceStub.text) from a TSV made by tools/prx_imports.py:
// stub_address <tab> name <tab> library <tab> nid. Creates a function at each stub.
// Usage (headless preScript or postScript): NameStubs.java <stubs.tsv>
// @category gd-game-24

import java.nio.file.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class NameStubs extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		int n = 0;
		for (String line : Files.readAllLines(Paths.get(args[0]))) {
			String[] f = line.split("\t");
			if (f.length < 2) {
				continue;
			}
			Address a = toAddr(Long.parseLong(f[0], 16));
			disassemble(a);
			Function fn = getFunctionAt(a);
			if (fn == null) {
				fn = createFunction(a, f[1]);
			}
			if (fn != null) {
				fn.setName(f[1], SourceType.IMPORTED);
				n++;
			}
			else {
				createLabel(a, f[1], true, SourceType.IMPORTED);
			}
		}
		println("NameStubs: named " + n + " stubs");
	}
}
