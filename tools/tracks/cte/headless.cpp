// Headless CrashTeamEditor: load an OBJ (with its .mtl and <name>_presets folder) and save LEV + VRM.
#include "level.h"
#include <cstdio>

struct HeadlessAccess
{
	static void Report(Level& l)
	{
		for (const auto& [name, msg] : l.m_invalidQuadblocks) { std::printf("  %s: %s\n", name.c_str(), msg.c_str()); }
		if (!l.m_logMessage.empty()) { std::printf("log: %s\n", l.m_logMessage.c_str()); }
	}
};

static Level lev;

int main(int argc, char* argv[])
{
	if (argc < 3) { std::fprintf(stderr, "usage: cte_headless <in.obj> <out dir>\n"); return 2; }
	bool ok = lev.Load(argv[1]);
	std::printf("load: %s, %zu quadblocks, %zu checkpoints, bsp %s\n", ok ? "ok" : "FAILED",
		lev.GetQuadblocks().size(), lev.GetCheckpoints().size(), lev.GetBSP().IsValid() ? "valid" : "INVALID");
	HeadlessAccess::Report(lev);
	if (!lev.GetBSP().IsValid())
	{
		int shown = 0;
		static BSP probe;
		std::vector<size_t> all;
		for (size_t i = 0; i < lev.GetQuadblocks().size(); i++) { all.push_back(i); }
		probe.SetQuadblockIndexes(all);
		probe.Generate(lev.GetQuadblocks(), 31, 64.0f);
		for (const BSP* b : probe.GetTree())
		{
			bool bad = b->IsBranch() ? (b->GetLeftChildren() == nullptr && b->GetRightChildren() == nullptr) : b->IsEmpty();
			if (!bad || shown++ > 5) { continue; }
			const BoundingBox& bb = b->GetBoundingBox();
			std::printf("  bad %s node: %zu quads, bbox (%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n", b->IsBranch() ? "branch" : "leaf",
				b->GetQuadblockIndexes().size(), bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z);
		}
	}
	if (!ok) { return 1; }
	bool saved = lev.Save(argv[2]);
	std::printf("save: %s\n", saved ? "ok" : "FAILED");
	HeadlessAccess::Report(lev);
	return saved ? 0 : 1;
}
