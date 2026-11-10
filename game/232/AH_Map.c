#include <common.h>

enum AHMapIconID
{
	AH_MAP_ICON_WARPPAD = 0x31,
	AH_MAP_ICON_BOSS_STAR = 0x37,
};

enum AHMapArrowType
{
	AH_MAP_ARROW_WARPPAD_TROPHY = 0,
	AH_MAP_ARROW_HUB_ROUTE,
	AH_MAP_ARROW_BOSS,
};

enum AHMapColor
{
	AH_MAP_COLOR_COMPLETE = RED,
	AH_MAP_COLOR_FLASH_PRIMARY = CRASH_BLUE,
	AH_MAP_COLOR_FLASH_SECONDARY = WHITE,
	AH_MAP_COLOR_RELIC_TOKEN = PAPU_YELLOW,
	AH_MAP_COLOR_INVALID = BLACK,
	AH_MAP_COLOR_LOCKED = GRAY,
};


enum AHMapBossItemState
{
	AH_MAP_BOSS_ITEM_NONE = -1,
	AH_MAP_BOSS_ITEM_LOCKED = 0,
	AH_MAP_BOSS_ITEM_OPEN = 1,
	AH_MAP_BOSS_ITEM_COMPLETE = 2,
};


enum AHMapMainConstants
{
	AH_MAP_HUD_AND_DEBUG_SPEEDOMETER = 0x8,
	AH_MAP_ICON_TOP_HALF = 3,
	AH_MAP_ICON_BOTTOM_HALF = 4,
	AH_MAP_SCREEN_POS_X = 500,
	AH_MAP_SCREEN_POS_Y = 195,
	AH_MAP_HUD_SLOT_SLIDE_METER = 8,
	AH_MAP_HUD_SLOT_RELIC_COUNT = 0xe,
	AH_MAP_HUD_SLOT_KEY_COUNT = 0xf,
	AH_MAP_HUD_SLOT_TROPHY_COUNT = 0x10,
	AH_MAP_HUD_COUNTER_OFFSET_X = 0x10,
	AH_MAP_HUD_COUNTER_OFFSET_Y = -10,
};

enum AHMapArrowOutlineConstants
{
	AH_MAP_ARROW_OUTLINE_COUNT = 3,
	AH_MAP_ARROW_OUTLINE_PHASE_STEP = 0xc,
	AH_MAP_ARROW_OUTLINE_PHASE_MASK = 0x3f,
	AH_MAP_ARROW_OUTLINE_PHASE_DELAY = 6,
	AH_MAP_ARROW_OUTLINE_VISIBLE_PHASES = 0xc,
	AH_MAP_ARROW_OUTLINE_RADIUS_MUL = 0x2aa,
	AH_MAP_ARROW_OUTLINE_RADIUS_BIAS = FP_ONE,
	AH_MAP_ARROW_OUTLINE_RADIUS_SHIFT = 0x1a,
	AH_MAP_ARROW_OUTLINE_TROPHY_STEP = 0x200,
	AH_MAP_ARROW_OUTLINE_ROUTE_STEP = 0x555,
	AH_MAP_ARROW_OUTLINE_BOSS_STEP = 0x199,
};


void AH_Map_LoadSave_Prim(const SVec2 *vertPos, char *vertCol, void *ot, struct PrimMem *primMem)
{
	POLY_G4 *cursor = primMem->cursor;
	POLY_G4 *p = NULL;

	if (primMem->guardEnd >= (void *)cursor)
	{
		p = cursor;
		primMem->cursor = p + 1;
	}
	if (p == NULL)
	{
		return;
	}

	setPolyG4(p);

	p->r0 = vertCol[0];
	p->g0 = vertCol[1];
	p->b0 = vertCol[2];

	p->r1 = vertCol[4];
	p->g1 = vertCol[5];
	p->b1 = vertCol[6];

	p->r2 = vertCol[8];
	p->g2 = vertCol[9];
	p->b2 = vertCol[10];

	p->r3 = vertCol[12];
	p->g3 = vertCol[13];
	p->b3 = vertCol[14];

	p->x0 = vertPos[0].x;
	p->y0 = vertPos[0].y;

	p->x1 = vertPos[1].x;
	p->y1 = vertPos[1].y;

	p->x2 = vertPos[2].x;
	p->y2 = vertPos[2].y;

	p->x3 = vertPos[3].x;
	p->y3 = vertPos[3].y;

	AddPrim(ot, p);
}

// NOTE(aalhendi): Both marker shapes narrow the transformed coordinates before
// adding outline offsets. Coordinate copies and the local point cursor preserve
// the retail compiler's lifetimes without changing that fixed-point arithmetic.
void AH_Map_LoadSave_Full(s32 inputX, s32 inputY, const SVec2 *positions, char *colors, s16 scale, s32 angle)
{
	s32 posX = inputX;
	s32 posY = inputY;
	s16 i;
	SVec2 basePos[4];
	SVec2 drawPos[4];
	register s32 sin CTR_PSX_REGISTER("$8");
	s32 cos;
	const struct TrigTable *table = AH_TRIG_TABLE;
	s32 scaleX;
	scaleX = (scale * 8) / 5;
	sin = CTR_ReadU32AlignedLE(&table[angle & 0x3ff]);
	if ((angle & 0x400) != 0)
	{
		cos = (s16)sin;
		sin = sin >> 16;
		if ((angle & 0x800) != 0)
			sin = -sin;
		else
			cos = -cos;
	}
	else
	{
		cos = sin >> 16;
		sin = (s16)sin;
		if ((angle & 0x800) != 0)
		{
			cos = -cos;
			sin = -sin;
		}
	}

	{
		SVec2 *transformed;
		i = 0;
		transformed = basePos;
		for (; i < 4; i++)
		{
			SVec2 *point = transformed + i;
			s32 x = (((((positions[i].x * cos) >> 12) + ((positions[i].y * sin) >> 12)) * (s16)scaleX) >> 12) + 6;
			s32 y;
			point->x = posX + x;
			y = (((((positions[i].y * cos) >> 12) - ((positions[i].x * sin) >> 12)) * scale) >> 12) + 4;
			point->y = posY + y;
		}
	}

	{
		for (i = 0; i < 5; i++)
		{
			{
				s16 j;
				for (j = 0; j < 4; j++)
				{
					drawPos[j].x = basePos[j].x + AH_SAVE_PRIM_OFFSETS[i].x;

					drawPos[j].y = basePos[j].y + AH_SAVE_PRIM_OFFSETS[i].y;
				}
			}

			AH_Map_LoadSave_Prim(&drawPos[0], colors, GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);

			colors = (char *)AH_QUAD_COLOR;
		}
	}
}

void AH_Map_HubArrow(s32 inputX, s32 inputY, const SVec2 *positions, char *colors, s16 scale, s32 angle)
{
	s32 posX = inputX;
	s32 posY = inputY;
	s16 i;
	SVec2 basePos[3];
	SVec2 drawPos[3];
	register s32 sin CTR_PSX_REGISTER("$8");
	s32 cos;
	const struct TrigTable *table = AH_TRIG_TABLE;
	s32 scaleX;
	scaleX = (scale * 8) / 5;
	sin = CTR_ReadU32AlignedLE(&table[angle & 0x3ff]);
	if ((angle & 0x400) != 0)
	{
		cos = (s16)sin;
		sin = sin >> 16;
		if ((angle & 0x800) != 0)
			sin = -sin;
		else
			cos = -cos;
	}
	else
	{
		cos = sin >> 16;
		sin = (s16)sin;
		if ((angle & 0x800) != 0)
		{
			cos = -cos;
			sin = -sin;
		}
	}

	{
		SVec2 *transformed;
		i = 0;
		transformed = basePos;
		for (; i < 3; i++)
		{
			SVec2 *point = transformed + i;
			s32 x = (((((positions[i].x * cos) >> 12) + ((positions[i].y * sin) >> 12)) * (s16)scaleX) >> 12) + 6;
			s32 y;
			point->x = posX + x;
			y = (((((positions[i].y * cos) >> 12) - ((positions[i].x * sin) >> 12)) * scale) >> 12) + 4;
			point->y = posY + y;
		}
	}

	{
		for (i = 0; i < 5; i++)
		{
			{
				s16 j;
				for (j = 0; j < 3; j++)
				{
					drawPos[j].x = basePos[j].x + AH_ARROW_PRIM_OFFSETS[i].x;

					drawPos[j].y = basePos[j].y + AH_ARROW_PRIM_OFFSETS[i].y;
				}
			}

			RECTMENU_DrawRwdTriangle(CTR_VECTOR_DATA(&(drawPos[0])), colors, GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);

			colors = (char *)AH_TRIANGLE_COLOR;
		}
	}
}

void AH_Map_HubArrowOuter(struct UIMap *map, s32 arrowIndex, s32 inputX, s32 inputY, s32 inputAngle, s32 type)
{
	register s32 posX CTR_PSX_REGISTER("$23");
	register s32 posY CTR_PSX_REGISTER("$22");
	const SVec2 *innerOffsets = AH_MAP_INNER_OFFSETS;
	u32 timer, phase;
	s32 red;
	s32 green;
	s32 blue;
	s32 step;
	s32 limit;
	s16 outlineIndex;
	s32 loopType;
	register s32 typeID CTR_PSX_REGISTER("$8");
	register s32 direction CTR_PSX_REGISTER("$9");
	s32 angleBits;
	(void)map;
	arrowIndex = (s16)arrowIndex;
	// NOTE(aalhendi): Preserve the full-width argument read before signed-halfword narrowing.
	CTR_PSX_OBSERVE_VALUE(type);
	{
		register s32 typeBits CTR_PSX_REGISTER("$2") = (s32)((u32)type << 16);
		typeID = typeBits >> 16;
	}
	{
		register s32 offsetX CTR_PSX_REGISTER("$3") = innerOffsets[typeID].x;
		register s32 offsetY CTR_PSX_REGISTER("$2") = innerOffsets[typeID].y;
		posX = inputX + offsetX;
		posY = inputY + offsetY;
	}
	timer = FPS_HALF(GAME_TRACKER->timer);
	phase = ~(timer + arrowIndex * AH_MAP_ARROW_OUTLINE_PHASE_STEP) & AH_MAP_ARROW_OUTLINE_PHASE_MASK;
	direction = inputAngle;
	switch (typeID)
	{
	case AH_MAP_ARROW_WARPPAD_TROPHY:
		green = (timer & 1) ? 0xe0 : 0x40;
		red = green;
		blue = 0xff;
		step = AH_MAP_ARROW_OUTLINE_TROPHY_STEP;
		break;
	case AH_MAP_ARROW_HUB_ROUTE:
	{
		s32 index;
		const SVec2 *routeOffsets;
		red = 0xff;
		blue = red;
		green = (timer & 1) ? 0xe0 : 0x40;
		step = AH_MAP_ARROW_OUTLINE_ROUTE_STEP;
		routeOffsets = AH_MAP_OUTER_OFFSETS;
		index = ((u32)inputAngle >> 10) & 3;
		posX += routeOffsets[index].x;
		posY += routeOffsets[index].y;
		break;
	}
	case AH_MAP_ARROW_BOSS:
	default:
		green = (FPS_HALF(GAME_TRACKER->timer) & 1) ? 0xe0 : 0x40;
		red = green;
		blue = 0xff;
		step = AH_MAP_ARROW_OUTLINE_BOSS_STEP;
		direction ^= 0x800;
		break;
	}
	outlineIndex = 0;
	limit = step + 0xfff;
	loopType = (s16)type;
	angleBits = (s32)((u32)direction << 16);
	for (; outlineIndex < AH_MAP_ARROW_OUTLINE_COUNT; outlineIndex++)
	{
		s32 radius;
		s32 radiusX;
		// NOTE(aalhendi): These are halfword loop states; boss outlines alternate
		// the radius at each vertex to form a star.
		b16 first;
		s16 toggle;
		s32 prevX, prevY;
		s16 outlineAngle;
		u32 localPhase = phase - (AH_MAP_ARROW_OUTLINE_COUNT - 1 - outlineIndex) * AH_MAP_ARROW_OUTLINE_PHASE_DELAY;
		if (localPhase >= AH_MAP_ARROW_OUTLINE_VISIBLE_PHASES)
			continue;
		radius = (s16)(localPhase * AH_MAP_ARROW_OUTLINE_RADIUS_MUL + AH_MAP_ARROW_OUTLINE_RADIUS_BIAS) >> 10;
		radiusX = (radius * 8) / 5;
		prevX = 0;
		first = true;
		toggle = first;
		outlineAngle = prevX;
		prevY = prevX;
		if (limit != 0)
		{
			s32 heading = angleBits >> 16;
			const struct TrigTable *table = AH_TRIG_TABLE;
			do
			{
				s32 angle, cos, shift;
				register s32 sin CTR_PSX_REGISTER("$3");
				register s32 x CTR_PSX_REGISTER("$17");
				s32 y;
				s32 cosineProduct;
				if (loopType != AH_MAP_ARROW_BOSS)
					toggle = 0;
				angle = outlineAngle + heading;
				sin = CTR_ReadU32AlignedLE(&table[angle & 0x3ff]);
				if (angle & 0x400)
				{
					cos = (s16)sin;
					sin >>= 16;
					if (angle & 0x800)
						sin = -sin;
					else
						cos = -cos;
				}
				else
				{
					cos = sin >> 16;
					sin = (s16)sin;
					if (angle & 0x800)
					{
						cos = -cos;
						sin = -sin;
					}
				}
				sin = radiusX * sin;

				cosineProduct = radius * cos;
				shift = (toggle & 1) + 12;
				sin >>= shift;
				y = posY - (cosineProduct >> shift);
				// NOTE(aalhendi): Keep the multiply result live through the Y transform.
				CTR_PSX_OBSERVE_VALUE(cosineProduct);
				x = posX + sin;
				if (!first)
				{
					register s32 wireRed CTR_PSX_REGISTER("$3") = (s16)red;
					CTR_Box_DrawWirePrims(prevX, prevY, x, y, wireRed, green, blue, GAME_TRACKER->pushBuffer_UI.ptrOT, &GAME_TRACKER->backBuffer->primMem);
				}
				prevX = x;
				prevY = y;
				first = false;
				outlineAngle += step;
				toggle++;
			} while (outlineAngle < limit);
		}
	}
}

void AH_Map_HubItems(struct UIMap *map, s16 *arrowCounter)
{
	struct HubItem *item = AH_MAP_HUB_ITEMS[GAME_TRACKER->levelID - GEM_STONE_VALLEY];
	Vec3 pos3D;

	if (item->posX != AH_HUB_ITEM_LIST_END_POS_X)
	{
		do
		{
			s16 bossState = AH_MAP_BOSS_ITEM_NONE;
			s16 routeLockState = -1;
			b32 open = true;
			s16 i;

			switch (item->iconType)
			{
			case AH_HUB_ITEM_SAVE_LOAD_MARKER:
			{
				s32 saveLoadPosX = item->posX - 0x200;
				s32 saveLoadPosY = item->posY - 0x100;
				UI_Map_GetIconPos(map, &saveLoadPosX, &saveLoadPosY);
				AH_Map_LoadSave_Full((s16)saveLoadPosX, (s16)saveLoadPosY, AH_MAP_SAVE_POS, (char *)AH_MAP_SAVE_COLORS, 0x800, item->angle);
				break;
			}
			case AH_HUB_ITEM_ROUTE_KEY3:
				routeLockState = GAME_TRACKER->currAdvProfile.numKeys < 3;
				break;
			case AH_HUB_ITEM_ROUTE_KEY2:
				routeLockState = GAME_TRACKER->currAdvProfile.numKeys < 2;
				break;
			case AH_HUB_ITEM_ROUTE_KEY1_IF_BEACH:
				routeLockState = 0;
				if (GAME_TRACKER->levelID == N_SANITY_BEACH)
					routeLockState = GAME_TRACKER->currAdvProfile.numKeys < 1;
				break;
			case AH_HUB_ITEM_ROUTE_OPEN_B:
				routeLockState = 0;
				break;
			case AH_HUB_ITEM_ROUTE_OPEN_A:
				routeLockState = 0;
				break;
			case AH_HUB_ITEM_RIPPER_ROO_GARAGE:
			case AH_HUB_ITEM_PAPU_PAPU_GARAGE:
			case AH_HUB_ITEM_KOMODO_JOE_GARAGE:
			case AH_HUB_ITEM_PINSTRIPE_GARAGE:
			{
				s32 base = (GAME_TRACKER->levelID - N_SANITY_BEACH) * AH_HUB_TRACK_COUNT * sizeof(s16);
#ifdef CTR_AP
				// AP: light the garage from the SAME authority its physical door
				// uses (AP_BossGarageOpen, see AH_Garage.c) so the map matches
				// whether it actually opens. AP fills trophy bits by COUNT (high-
				// end first), never the specific hub-track bits the vanilla loop
				// checks -- so that loop keeps the garage dark on the map even when
				// the door opens (e.g. 4 trophies received). The boss index is
				// levelID - N_SANITY_BEACH (GEM_STONE_VALLEY and N_SANITY_BEACH are
				// consecutive level IDs).
				if (ctr_cfg_active())
				{
					open = (AP_BossGarageOpen(GAME_TRACKER->levelID - N_SANITY_BEACH) != 0);
				}
				else
#endif
				{
					for (i = 0; i < AH_HUB_TRACK_COUNT; i++)
					{
						if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, *(s16 *)((u8 *)AH_HUB_TRACK_IDS + base + i * sizeof(s16)) + ADV_REWARD_FIRST_TROPHY))
						{
							open = false;
							break;
						}
					}
				}
				if ((s16)open)
				{
					b32 beaten;
					bossState = AH_MAP_BOSS_ITEM_OPEN;
#ifdef CTR_AP
					// AP: "beaten" must key off the boss-race LOCATION being
					// checked, not the key AdvProgress bit -- in AP those bits
					// double as the RECEIVED-Key pool, so holding Keys must not
					// paint the star and a garage you personally beat must read
					// done. Override, not OR: the AdvProgress mirror is rewritten
					// from received Keys every frame. #458 follow-up: under a
					// Bosses goal that still needs bosses, "beaten" is the
					// player's own win, so a boss checked by another player's
					// Collect keeps its glow (AP_BossMapStarBeaten).
					if (ctr_cfg_active())
						beaten = AP_BossMapStarBeaten(GAME_TRACKER->levelID - N_SANITY_BEACH) != 0;
					else
#endif
						beaten = CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, GAME_TRACKER->levelID + ADV_REWARD_FIRST_BOSS_KEY - N_SANITY_BEACH) != 0;
					if (beaten)
					{
						bossState = AH_MAP_BOSS_ITEM_COMPLETE;
#ifdef CTR_AP
						// AP: a checked boss-race location removes the star
						// entirely. Garages only; the Oxide icon keeps its marker.
						if (ctr_cfg_active())
							bossState = AH_MAP_BOSS_ITEM_NONE;
#endif
					}
				}
				else
					bossState = AH_MAP_BOSS_ITEM_LOCKED;
				break;
			}
			case AH_HUB_ITEM_OXIDE_WARPPAD:
#ifdef CTR_AP
				// AP (WO-A1): light Oxide's garage from the SAME authority
				// its physical door uses (AP_OxideGarageOpen, AH_Garage.c),
				// exactly as the four boss garages above already delegate to
				// AP_BossGarageOpen. The vanilla loop reads the four key bits,
				// which AP_ApplyItems rewrites from RECEIVED Keys every
				// reconcile tick -- so it would paint the garage open on Key 4
				// while the composed-goal gate correctly keeps the door shut.
				if (ctr_cfg_active())
				{
					open = (AP_OxideGarageOpen() != 0);
				}
				else
#endif
				{
					for (i = 0; i < AH_BOSS_KEY_COUNT; i++)
					{
						if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, i + ADV_REWARD_FIRST_BOSS_KEY))
						{
							open = false;
							break;
						}
					}
				}
				if ((s16)open)
				{
					bossState = AH_MAP_BOSS_ITEM_OPEN;
					if (GAME_ADV_PROGRESS.rewards[ADV_PROGRESS_WORD_STORY] & ADV_REWARD_BEAT_OXIDE_FIRST_BOSS_MASK)
						bossState = AH_MAP_BOSS_ITEM_COMPLETE;
				}
				else
					bossState = AH_MAP_BOSS_ITEM_LOCKED;
				break;
			}

			if (routeLockState >= 0)
			{
				int routePosX = (int)item->posX - 0x200;
				int routePosY = (int)item->posY - 0x100;

				UI_Map_GetIconPos(map, &routePosX, &routePosY);
				if ((routeLockState == 0) && (AH_MAP_ARROW_DRAWN == 0))
				{
					AH_Map_HubArrowOuter(map, (int)*arrowCounter, routePosX, routePosY, (s16)(0x1000 - (u16)item->angle), AH_MAP_ARROW_HUB_ROUTE);
					*arrowCounter = *arrowCounter + 1;
				}

				{
					SVec2 *arrowPos = AH_MAP_ARROW_POS;
					register s32 colorByteOffset CTR_PSX_REGISTER("$3") = routeLockState;
					s32 twiceState = colorByteOffset * 2;
					s32 x = (s16)routePosX;
					s32 y = (s16)routePosY;
					char *colors;

					// Two flashing RGB triplets for each route state.
					if (FPS_HALF(GAME_TRACKER->timer) & 2)
						colorByteOffset = (twiceState + 1) * 3;
					else
						colorByteOffset = colorByteOffset * 4 + twiceState;
					colorByteOffset *= sizeof(u32);
					colors = (char *)AH_MAP_ARROW_COLORS + colorByteOffset;

					// NOTE(aalhendi): Keep the color address complete before preparing
					// the stack arguments; native needs no scheduling constraint.
					CTR_PSX_OBSERVE_VALUE(colors);
					AH_Map_HubArrow(x, y, arrowPos, colors, 0x800, item->angle);
				}
			}

			if (bossState >= AH_MAP_BOSS_ITEM_LOCKED)
			{
				s16 bossIconColor;
				pos3D.x = (int)item->posX;
				pos3D.y = 0;
				pos3D.z = (int)item->posY;

				if (bossState != AH_MAP_BOSS_ITEM_COMPLETE)
				{
					if (bossState == AH_MAP_BOSS_ITEM_OPEN)
						bossIconColor = (FPS_HALF(GAME_TRACKER->timer) & 2) ? AH_MAP_COLOR_FLASH_SECONDARY : AH_MAP_COLOR_FLASH_PRIMARY;
					else
						bossIconColor = AH_MAP_COLOR_LOCKED;
				}
				else
				{
					bossIconColor = AH_MAP_COLOR_COMPLETE;
				}

				{
					s32 state = bossState;

					// Open, not beaten: point towards this boss.
					if (state == AH_MAP_BOSS_ITEM_OPEN)
					{
						int bossArrowPosX;
						int bossArrowPosY;
						AH_MAP_ARROW_DRAWN = state;

						bossArrowPosX = pos3D.x;
						bossArrowPosY = pos3D.z;

						UI_Map_GetIconPos(map, &bossArrowPosX, &bossArrowPosY);

						AH_Map_HubArrowOuter(map, (int)*arrowCounter, bossArrowPosX, bossArrowPosY, 0, AH_MAP_ARROW_BOSS);
						*arrowCounter = *arrowCounter + 1;
					}
				}

				// draw star icon for boss
				UI_Map_DrawRawIcon(map, (const VecElement *)&pos3D, AH_MAP_ICON_BOSS_STAR, bossIconColor, 0, 0x1000);
			}
			item++;
		} while (item->posX != AH_HUB_ITEM_LIST_END_POS_X);
	}
}

#ifdef CTR_AP
// UI_Map_DrawRawIcon with one exact vertex colour (0x00BBGGRR) on all four
// corners instead of a font palette slot: the pad dot of the shared pad colour
// table (ap_pad_colour.h). Same icon, position and scale as the retail call.
static void AH_Map_DrawPadIconRGB(struct UIMap *map, const s32 *pos, u32 vertex)
{
	struct GameTracker *gGT = GAME_TRACKER;
	s32 posX = pos[0];
	s32 posY = pos[2];
	struct Icon **iconPtrArray = ICONGROUP_GETICONS(gGT->iconGroup[5]);

	Color c = {0};

	c.r = (u8)(vertex & 0xFF);
	c.g = (u8)((vertex >> 8) & 0xFF);
	c.b = (u8)((vertex >> 16) & 0xFF);
	UI_Map_GetIconPos(map, &posX, &posY);
	DecalHUD_DrawPolyGT4(iconPtrArray[AH_MAP_ICON_WARPPAD], posX, posY, &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT,
	                     c, c, c, c, 0, 0x1000);
}
#endif

void AH_Map_Warppads(struct UIMap *map, struct Thread *warppadThread, s16 *arrowCounter)
{
	struct Instance *closestWarppadInst = NULL;
	int minDistance = 0x7fffffff;

	for (
	    /**/; warppadThread != NULL; warppadThread = warppadThread->siblingThread)
	{
		Vec3 delta;
		int currDistance;
		int visualState = warppadThread->modelIndex;
		b16 drawsTrophyArrow = false;
		b16 skipSound = false;

		struct Instance *warppadInst = warppadThread->inst;
		s16 color;
#ifdef CTR_AP
		u32 apMapVertex;
#endif

		switch ((u32)visualState)
		{
		case AH_WP_VISUAL_LOCKED:
			skipSound = true;
			color = AH_MAP_COLOR_LOCKED;
			break;
		case AH_WP_VISUAL_TROPHY_OPEN:
			color = AH_MAP_COLOR_FLASH_PRIMARY;
			if ((FPS_HALF(GAME_TRACKER->timer) & 2) != 0)
			{
				color = AH_MAP_COLOR_FLASH_SECONDARY;
			}
			drawsTrophyArrow = true;
			break;
		case AH_WP_VISUAL_COMPLETE:
			color = AH_MAP_COLOR_COMPLETE;
			break;
		case AH_WP_VISUAL_RELIC_TOKEN_OPEN:
			color = AH_MAP_COLOR_RELIC_TOKEN;
			break;
		case AH_WP_VISUAL_COLOR_CYCLE_OPEN:
			// Each Slide Coliseum/Turbo Track color lasts two frames.
			color = (((u32)FPS_HALF(GAME_TRACKER->timer) >> 1) & 7) + AH_MAP_COLOR_FLASH_PRIMARY;
			break;
		default:
			skipSound = true;
			color = AH_MAP_COLOR_INVALID;
			break;
		}

#ifdef CTR_AP
		// AP Warp-Pad State Model v2: one honest map, always on. Whenever slot_data
		// is active, EVERY warp-pad icon (locked ones included -- vanilla skipped
		// them via skipDistance) is coloured from its pad's display state
		// (AP_PadLook: AP_PadState of its DESTINATION track plus the racer fact),
		// so the player reads the pad's state off the map without a mode switch.
		// Pure colour override -- no geometry change, and skipDistance still
		// governs the proximity-sound distance check below (gameplay untouched).
		//
		// Alpha4: the colours are the shared green/red/grey table of
		// ap_pad_colour.h, the same as the warp pad beam and the tracker node, so
		// they are drawn as exact vertex colours rather than font palette slots.
		// Closed and waiting red, both open phases green while a displayed
		// check is in logic, Done grey.
		//
		// Animated-cue vocabulary (issue #60 -- keep these distinct):
		//   static colour        = nothing actionable here right now
		//   GREEN fast flicker    = trophy check is open (race it)
		//   GREEN slow pulse     = trophy done, but tier-2 checks (relic/CTR)
		//                           remain -- pad stays "alive" instead of going
		//                           quiet the moment the trophy is checked
		// The flicker alternates the state colour with a darker shade of the same
		// hue. The map_flash option (ap-config.txt / Options) turns it off;
		// AP_MapFlashOn() defaults to 1 (flicker on).
		// AP_PadLook returns AP_PAD_LOOK_NONE in vanilla mode / for an
		// unrecognised destination, so the vanilla colour switch above is left
		// entirely untouched there.
		apMapVertex = 0;
		{
			int destLevelID = ((struct WarpPad *)warppadThread->object)->levelID;
			int physLevelID = ctr_cfg_warp_phys(destLevelID);
			int look = AP_PadLook(physLevelID, destLevelID);
			int dim = 0;
			if (look == AP_PAD_LOOK_OPEN)
				dim = AP_MapFlashOn() && (FPS_HALF(GAME_TRACKER->timer) & 2) != 0;
			else if (look == AP_PAD_LOOK_STAGE2_OPEN)
				dim = AP_MapFlashOn() && (FPS_HALF(GAME_TRACKER->timer) & 4) != 0;
			if (look != AP_PAD_LOOK_NONE)
				apMapVertex = AP_PadLookMapVertex(look, dim);
		}
#endif

		if (color >= 0)
		{
			if ((s16)drawsTrophyArrow)
			{
				// get posZ in 3D, turns into posY in 2D
				int arrowPosX = warppadInst->matrix.t[0];
				int arrowPosY = warppadInst->matrix.t[2];

				AH_MAP_ARROW_DRAWN = 1;

				// Get Icon Dimensions
				UI_Map_GetIconPos(map, &arrowPosX, &arrowPosY);

				AH_Map_HubArrowOuter(map, (int)*arrowCounter, arrowPosX, arrowPosY, 0, AH_MAP_ARROW_WARPPAD_TROPHY);

				*arrowCounter = *arrowCounter + 1;
			}

#ifdef CTR_AP
			if (apMapVertex != 0)
				AH_Map_DrawPadIconRGB(map, &warppadInst->matrix.t[0], apMapVertex);
			else
#endif
			UI_Map_DrawRawIcon(map, &warppadInst->matrix.t[0], AH_MAP_ICON_WARPPAD, color, 0, 0x1000);
		}

		if ((s16)skipSound)
		{
			// skip distance check
			continue;
		}

		delta.x = warppadInst->matrix.t[0] - GAME_TRACKER->drivers[0]->instSelf->matrix.t[0];
		delta.y = warppadInst->matrix.t[1] - GAME_TRACKER->drivers[0]->instSelf->matrix.t[1];
		delta.z = warppadInst->matrix.t[2] - GAME_TRACKER->drivers[0]->instSelf->matrix.t[2];

		currDistance = SquareRoot0_stub(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

		if (minDistance > currDistance)
		{
			minDistance = currDistance;
			closestWarppadInst = warppadInst;
		}
	}

	// play sound from closest unlocked warppad
	if (closestWarppadInst != NULL)
	{
		PlayWarppadSound(minDistance << 1);
	}

	return;
}

#if defined(CTR_NATIVE)
static void AH_MaskHint_DrawRepeatPrompt(void);
#endif

void AH_Map_Main(void)
{
	s16 driverIconCounter;
	s16 arrowCounter;
	struct Driver *advDriver;
	struct UiElement2D *hud;
	struct UIMap *map;
	s32 raceFlagState;
	struct GameTracker *gGT = GAME_TRACKER;
	s32 playerCount;

	playerCount = gGT->numPlyrCurrGame;
	driverIconCounter = 0;
	AH_HUD_FLAGS &= ~AH_MAP_HUD_AND_DEBUG_SPEEDOMETER;
	advDriver = gGT->drivers[0];
	hud = AH_HUD_LAYOUTS[playerCount - 1];
	map = NULL;

	raceFlagState = RaceFlag_GetCanDraw();
	if (raceFlagState == 0)
	{
		RaceFlag_SetCanDraw(1);
	}

	if (
	    // if Aku Hint is not unlocked
	    (AH_STORY_REWARDS & ADV_REWARD_HINT_WELCOME_TO_ARENA_MASK) == 0 &&

	    RaceFlag_IsFullyOffScreen())
	{
		// Trigger Aku Hint:
		// Welcome to Adventure Arena
		MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_WELCOME_TO_ARENA, 0);
	}


	// NOTE(aalhendi): Retail keeps this AI-only Adventure Hub speedometer fallback.
	if ((GAME_TRACKER->numPlyrCurrGame == 0) && ((GAME_TRACKER->drivers[0]->actionsFlagSet & ACTION_BOT) != 0))
	{
		AH_HUD_FLAGS = AH_MAP_HUD_AND_DEBUG_SPEEDOMETER;
	}

	if (GAME_TRACKER->level1->ptrSpawnType1->count != 0)
	{
		void **pointers = ST1_GETPOINTERS(GAME_TRACKER->level1->ptrSpawnType1);
		map = pointers[ST1_MAP];
	}

	// if game is not paused
	if ((GAME_TRACKER->gameMode1 & PAUSE_ALL) == 0)
	{
		// Jump meter and landing boost
		UI_JumpMeter_Update(advDriver);
	}

	if ((GAME_TRACKER->hudFlags & HUD_FLAG_HIDE_ADVENTURE_MAP) == 0
#ifdef CTR_AP
	    // The hub minimap and its markers hide while the character picker is
	    // open (#54/#209). They drew ON TOP of it: the map goes to the UI
	    // ordering table and the picker's own draw call is further down this
	    // same function, so the picker's portraits are submitted AFTER the map,
	    // which under the ordering table's prepend-and-walk places them behind
	    // it. The map's fixed corner sits squarely over the right-hand tiles.
	    //
	    // Suppressing the draw is the honest fix rather than shuffling
	    // submission order between two tables: the picker is a full-screen
	    // modal that already holds VEH_FREEZE_DOOR, so there is nothing a
	    // minimap can usefully say while it is up. This covers the whole group
	    // -- driver dots, warp pads, hub items, the map itself and the slide
	    // meter -- because all of them overlay the same corner of the screen.
	    //
	    // Asked through AP_CharSwap_HubHudHidden rather than the picker flag
	    // directly, so this site and the counters below cannot drift apart.
	    && !AP_CharSwap_HubHudHidden()
#endif
	)
	{
		arrowCounter = 0;

		AH_MAP_ARROW_DRAWN = 0;

		UI_Map_DrawDrivers(map, GAME_TRACKER->threadBuckets[PLAYER].thread, &driverIconCounter);

		AH_Map_Warppads(map, GAME_TRACKER->threadBuckets[WARPPAD].thread, &arrowCounter);

		AH_Map_HubItems(map, &arrowCounter);

		UI_Map_DrawMap(GAME_TRACKER->ptrIcons[AH_MAP_ICON_TOP_HALF], GAME_TRACKER->ptrIcons[AH_MAP_ICON_BOTTOM_HALF],

		               AH_MAP_SCREEN_POS_X, AH_MAP_SCREEN_POS_Y,

		               &GAME_TRACKER->backBuffer->primMem, GAME_TRACKER->pushBuffer_UI.ptrOT, 1);

		UI_DrawSlideMeter(hud[AH_MAP_HUD_SLOT_SLIDE_METER].x, hud[AH_MAP_HUD_SLOT_SLIDE_METER].y, advDriver);
	}

#ifdef CTR_AP
	// The relic / key / trophy counters are the same class of standing hub HUD
	// as the map group above and hide with it. They sit across the TOP of the
	// screen, where the map guard does not reach, and the picker's own name and
	// portrait rows are drawn under them.
	if (!AP_CharSwap_HubHudHidden())
#endif
	{
		UI_DrawNumRelic(hud[AH_MAP_HUD_SLOT_RELIC_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_RELIC_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y,
		                advDriver);
		UI_DrawNumKey(hud[AH_MAP_HUD_SLOT_KEY_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_KEY_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y, advDriver);
		UI_DrawNumTrophy(hud[AH_MAP_HUD_SLOT_TROPHY_COUNT].x + AH_MAP_HUD_COUNTER_OFFSET_X, hud[AH_MAP_HUD_SLOT_TROPHY_COUNT].y + AH_MAP_HUD_COUNTER_OFFSET_Y,
		                 advDriver);
#ifdef CTR_AP
		// #249: Wumpa balance and banked total below the counters.
		AP_WumpaDrawHub();
#endif
	}

#ifdef CTR_AP
	// AP hub item-received feed (bottom-left). Also drawn from the transient
	// hub-entry pass (UI_RenderFrame_AdvHub); the two passes are mutually
	// exclusive per frame (RenderAllHUD if/else on LOAD_IsOpen_AdvHub), so the
	// feed still ticks exactly once per frame. Without this call the feed dies
	// the moment the walkable 232 overlay takes over (e.g. when the trophy-won
	// entry animation ends).
	AP_FeedDrawHub();
	// Newer-schema warning (issue #8): top-left RED banner, self-gates on
	// ctr_cfg.schema_newer. No-op unless the seed is from a newer apworld.
	AP_DrawSchemaWarning();
	// Seed-verify warning: RED banner when a SOLO seed's goal is provably
	// unreachable from the current state (ap_verify.c). Self-gates.
	AP_DrawVerifyWarning();
#ifdef CTR_CUSTOM_TRACKS
	AP_DrawCustomContentWarning();
#endif
	// Hub character picker (spike, #54/R7). Self-gates on dev_keys + "picker
	// open", and rides the same once-per-frame hub UI pass as the feed above.
	AP_CharPicker_Draw();
#endif

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail appends this prompt after DrawOTag starts; the PS1
	// GPU can still consume that late OT write. Native DrawOTag parses
	// synchronously, so emit only this static prompt during the hub UI pass and
	// leave AH_MaskHint_Update to run the real state/audio timing later.
	if (sdata->AkuAkuHintState == 5)
	{
		AH_MaskHint_DrawRepeatPrompt();
	}
#endif
}
