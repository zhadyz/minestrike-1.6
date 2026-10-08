// The Minecraft survival inventory: 27 storage slots, the 2x2 crafting grid, armor slots and the stack
// on the mouse cursor, with Minecraft's click rules (InventoryMenu / AbstractContainerMenu.doClick).
// Slot numbers follow Minecraft's InventoryMenu: 0 craft result, 1-4 craft grid, 5-8 armor (head..feet),
// 9-35 storage, 36-44 hotbar, 45 offhand, plus 46 = trash. Counter-Strike guns are tokens (mci::IsCsToken)
// that move between hotbar and storage like items; thrown out of the window they drop like CS's drop.
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
void UpdatePlayerFlagsPublic(CBasePlayer* pl);
int CraftResult(const mci::Stack craft[4], int* count); // mc_crafting (simple 2x2 recipes)

static mci::Stack* SlotRef(McPlayer& mp, int slot)
{
	if (slot >= 1 && slot <= 4)
		return &mp.craft[slot - 1];
	if (slot >= 5 && slot <= 8)
		return &mp.armor[slot - 5];
	if (slot >= 9 && slot <= 35)
		return &mp.inv[slot - 9];
	if (slot >= 36 && slot <= 44)
		return &mp.hotbar[slot - 36];
	return nullptr;
}

// Can this item go into the slot? (armor slots take matching armor only)
static bool Accepts(int slot, int itemId)
{
	if (mci::IsCsToken(itemId))
		return slot >= 9 && slot <= 44; // guns: hotbar and storage only
	if (slot >= 5 && slot <= 8)
	{
		const mci::ItemDef& d = mci::Item(itemId);
		if (d.type != mci::IT_ARMOR && d.type != mci::IT_ELYTRA)
			return false;
		return d.armorSlot == slot - 5;
	}
	return slot != 0;
}

static int MaxStack(int slot, int itemId)
{
	if (mci::IsCsToken(itemId))
		return 1;
	if (slot >= 5 && slot <= 8)
		return 1;
	int m = mci::Item(itemId).maxStack;
	return m > 0 ? m : 1;
}

static void ArmorChanged(CBasePlayer* pl, int itemId)
{
	McPlayer& mp = P(pl);
	if (itemId)
	{
		const mci::ItemDef& d = mci::Item(itemId);
		int snd = d.type == mci::IT_ELYTRA ? mcs::MCS_EQUIP_ELYTRA
			: (!strncmp(d.name, "diamond", 7) || !strncmp(d.name, "netherite", 9)) ? mcs::MCS_EQUIP_DIAMOND
			: mcs::MCS_EQUIP_GENERIC;
		FxSound(snd, pl->pev->origin, 1.0f, 1.0f, pl->entindex());
		if (d.type == mci::IT_ARMOR)
		{
			pl->pev->armorvalue = 0;
			pl->m_iKevlar = ARMOR_NONE;
		}
	}
	mp.statDirty = true;
	UpdatePlayerFlagsPublic(pl);
}

void ThrowStack(CBasePlayer* pl, const mci::Stack& s, bool scatter)
{
	if (s.Empty())
		return;
	Vector org, vel;
	if (scatter)
	{
		// Player.dropAll: items burst out in random directions
		org = pl->pev->origin + Vector(0, 0, 10);
		float a = RANDOM_FLOAT(0.0f, 6.2831853f), sp = RANDOM_FLOAT(0.0f, 0.5f) * 40.0f * 20.0f * 0.3f;
		vel = Vector(cosf(a) * sp, sinf(a) * sp, RANDOM_FLOAT(120.0f, 200.0f));
	}
	else
	{
		UTIL_MakeVectors(pl->pev->v_angle);
		org = pl->pev->origin + pl->pev->view_ofs + gpGlobals->v_forward * 16.0f - Vector(0, 0, 12);
		vel = gpGlobals->v_forward * 240.0f + Vector(0, 0, 80) + pl->pev->velocity * 0.5f;
	}
	float o[3] = {org.x, org.y, org.z}, v[3] = {vel.x, vel.y, vel.z};
	CBaseEntity* e = SpawnItemEntity(o, s.id, s.count, v);
	if (e)
		SetItemDamage(e, s.damage);
}

static void Changed(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	mp.invDirty = true;
	UpdatePlayerFlagsPublic(pl);
}

// Shift-click: storage <-> hotbar, armor to its slot, armor slot / craft grid to storage.
static void QuickMove(CBasePlayer* pl, int slot)
{
	McPlayer& mp = P(pl);
	mci::Stack* src = SlotRef(mp, slot);
	if (!src || src->Empty())
		return;
	const mci::ItemDef& d = mci::Item(src->id);
	auto moveInto = [&](int from, int to) {
		for (int pass = 0; pass < 2 && !src->Empty(); pass++)
			for (int t = from; t <= to && !src->Empty(); t++)
			{
				if (t == slot || !Accepts(t, src->id))
					continue;
				mci::Stack* dst = SlotRef(mp, t);
				if (!dst)
					continue;
				int max = MaxStack(t, src->id);
				if (pass == 0 && !dst->Empty() && dst->id == src->id && dst->damage == src->damage && dst->count < max)
				{
					int n = min((int)src->count, max - (int)dst->count);
					dst->count += n;
					src->count -= n;
				}
				else if (pass == 1 && dst->Empty())
				{
					int n = min((int)src->count, max);
					*dst = *src;
					dst->count = (uint8_t)n;
					src->count -= n;
				}
			}
		if (src->count == 0)
			*src = mci::Stack();
	};
	bool wasArmor = slot >= 5 && slot <= 8;
	if ((d.type == mci::IT_ARMOR || d.type == mci::IT_ELYTRA) && !wasArmor && SlotRef(mp, 5 + d.armorSlot)->Empty())
	{
		int id = src->id;
		moveInto(5 + d.armorSlot, 5 + d.armorSlot);
		ArmorChanged(pl, id);
	}
	else if (slot >= 9 && slot <= 35)
		moveInto(36, 44);
	else if (slot >= 36)
		moveInto(9, 35);
	else
	{
		moveInto(9, 44);
		if (wasArmor)
			ArmorChanged(pl, 0);
	}
	Changed(pl);
}

void InventoryClick(CBasePlayer* pl, int slot, int button, bool shift)
{
	McPlayer& mp = P(pl);
	mci::Stack& cur = mp.cursor;
	McLog("inv click slot %d button %d shift %d (cursor %s x%d)", slot, button, (int)shift, cur.Empty() ? "-" : mci::Item(cur.id).name, cur.count);
	if (slot == -999)
	{
		// clicked outside the window: throw the cursor stack (right click throws one)
		if (cur.Empty())
			return;
		if (mci::IsCsToken(cur.id))
		{
			// a gun: drop it like CS (its token goes away once the weapon is gone)
			if (DropCsWeapon(pl, cur.id - mci::CS_TOKEN_BASE))
				cur = mci::Stack();
			Changed(pl);
			return;
		}
		if (button == 1)
		{
			mci::Stack one = cur;
			one.count = 1;
			ThrowStack(pl, one, false);
			if (--cur.count == 0)
				cur = mci::Stack();
		}
		else
		{
			ThrowStack(pl, cur, false);
			cur = mci::Stack();
		}
		Changed(pl);
		return;
	}
	if (slot == 0)
	{
		// craft result: take it (shift: as many as fit), consuming one of each ingredient per craft
		int n = 0;
		int id = CraftResult(mp.craft, &n);
		if (id <= 0)
			return;
		for (int guard = 0; guard < 64; guard++)
		{
			if (shift)
			{
				if (!GiveItem(pl, id, n, false, true))
					break;
			}
			else
			{
				if (!cur.Empty() && (cur.id != id || cur.count + n > mci::Item(id).maxStack))
					break;
				if (cur.Empty())
				{
					cur.id = (uint16_t)id;
					cur.count = (uint8_t)n;
					cur.damage = 0;
				}
				else
					cur.count += n;
			}
			for (auto& c : mp.craft)
				if (!c.Empty() && --c.count == 0)
					c = mci::Stack();
			int n2 = 0;
			if (!shift || CraftResult(mp.craft, &n2) != id)
				break;
		}
		FxSound(mcs::MCS_UI_CLICK, pl->pev->origin, 0.3f, 1.0f, pl->entindex());
		Changed(pl);
		return;
	}
	if (slot == 46)
	{
		// trash: whatever is on the cursor is gone (not guns: drop those)
		if (!cur.Empty() && !mci::IsCsToken(cur.id))
		{
			McLog("inv: trashed %d x %s", cur.count, mci::Item(cur.id).name);
			cur = mci::Stack();
			FxSound(mcs::MCS_ITEM_BREAK, pl->pev->origin, 0.3f, 1.4f, pl->entindex());
			Changed(pl);
		}
		return;
	}
	mci::Stack* s = SlotRef(mp, slot);
	if (!s)
		return;
	if (shift)
	{
		QuickMove(pl, slot);
		return;
	}
	bool armorSlot = slot >= 5 && slot <= 8;
	int armorBefore = armorSlot ? s->id : 0;
	if (button == 0)
	{
		if (cur.Empty())
		{
			cur = *s; // pick up the whole stack
			*s = mci::Stack();
		}
		else if (s->Empty())
		{
			if (!Accepts(slot, cur.id))
				return;
			int max = MaxStack(slot, cur.id);
			*s = cur;
			s->count = (uint8_t)min((int)cur.count, max);
			cur.count -= s->count;
			if (cur.count == 0)
				cur = mci::Stack();
		}
		else if (s->id == cur.id && s->damage == cur.damage)
		{
			int max = MaxStack(slot, cur.id);
			int n = min((int)cur.count, max - (int)s->count);
			if (n > 0)
			{
				s->count += n;
				cur.count -= n;
				if (cur.count == 0)
					cur = mci::Stack();
			}
		}
		else if (Accepts(slot, cur.id) && cur.count <= MaxStack(slot, cur.id))
		{
			mci::Stack t = *s;
			*s = cur;
			cur = t;
		}
	}
	else
	{
		if (cur.Empty())
		{
			if (s->Empty())
				return;
			int half = (s->count + 1) / 2; // take the bigger half
			cur = *s;
			cur.count = (uint8_t)half;
			s->count -= half;
			if (s->count == 0)
				*s = mci::Stack();
		}
		else if (s->Empty() || (s->id == cur.id && s->damage == cur.damage && s->count < MaxStack(slot, cur.id)))
		{
			if (!Accepts(slot, cur.id))
				return;
			if (s->Empty())
			{
				*s = cur;
				s->count = 0;
			}
			s->count++;
			if (--cur.count == 0)
				cur = mci::Stack();
		}
		else if (Accepts(slot, cur.id) && cur.count <= MaxStack(slot, cur.id))
		{
			mci::Stack t = *s;
			*s = cur;
			cur = t;
		}
	}
	if (armorSlot && s->id != armorBefore)
		ArmorChanged(pl, s->id);
	Changed(pl);
}

// Q over a slot: throw one (ctrl: the whole stack).
void InventoryDrop(CBasePlayer* pl, int slot, bool all)
{
	McPlayer& mp = P(pl);
	mci::Stack* s = SlotRef(mp, slot);
	if (!s || s->Empty())
		return;
	if (mci::IsCsToken(s->id))
	{
		if (DropCsWeapon(pl, s->id - mci::CS_TOKEN_BASE))
			*s = mci::Stack();
		Changed(pl);
		return;
	}
	mci::Stack out = *s;
	if (!all)
		out.count = 1;
	ThrowStack(pl, out, false);
	s->count -= out.count;
	if (s->count == 0)
		*s = mci::Stack();
	if (slot >= 5 && slot <= 8)
		ArmorChanged(pl, 0);
	Changed(pl);
}

// Closing the screen: the cursor stack and the crafting grid go back into the inventory (or drop).
void InventoryClose(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	auto giveBack = [&](mci::Stack& s) {
		if (s.Empty())
			return;
		if (mci::IsCsToken(s.id))
		{
			s = mci::Stack(); // SyncCsTokens puts it back where it last was
			return;
		}
		mci::Stack t = s;
		s = mci::Stack();
		if (!GiveItem(pl, t.id, t.count, false, true))
			ThrowStack(pl, t, false);
	};
	giveBack(mp.cursor);
	for (auto& c : mp.craft)
		giveBack(c);
	Changed(pl);
}

// Death: everything Minecraft scatters on the ground (Player.dropAll).
void DropEverything(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	// (guns are dropped by Counter-Strike itself; their tokens stay to keep the layout for next life)
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
	{
		if (mci::IsCsToken(mp.hotbar[i].id))
			continue;
		ThrowStack(pl, mp.hotbar[i], true);
		mp.hotbar[i] = mci::Stack();
	}
	for (auto& s : mp.inv)
	{
		if (mci::IsCsToken(s.id))
			continue;
		ThrowStack(pl, s, true);
		s = mci::Stack();
	}
	for (auto& s : mp.armor)
	{
		ThrowStack(pl, s, true);
		s = mci::Stack();
	}
	for (auto& s : mp.craft)
	{
		ThrowStack(pl, s, true);
		s = mci::Stack();
	}
	if (!mci::IsCsToken(mp.cursor.id))
		ThrowStack(pl, mp.cursor, true);
	mp.cursor = mci::Stack();
	mp.invDirty = mp.statDirty = true;
	UpdatePlayerFlagsPublic(pl);
}
} // namespace mc
