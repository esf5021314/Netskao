// 模块说明：
// 本修改器用到的魔兽争霸 III 内部结构。命名与字段写法沿用 TuringY 的 GameStructs.h / UIStructs.h，
// 只保留本工程实际访问的字段，其余用 unk_xxx 占位，保证字段偏移正确。
//
// 访问方式（与 TuringY 一致）：
//     war3::CGameUI* ui = GameUIObjectGet();
//     if (!ui) return;
//     war3::CWorldFrameWar3* world = GameUIField(ui->world);
//
// 偏移核对记录（1.24E / 6387）：
//   CGameUI          构造函数 0x2FF530，0x301270 中 new 0x454 字节并写入全局 [0xACBDD8]
//   CGameUI::world   构造函数 0x2FF827 调用 CWorldFrameWar3 构造（0x39B450）后 mov [esi+3BC],eax
//   CWorldFrameWar3  RTTI .?AVCWorldFrameWar3@@，虚表 0x6F9536D4；构造函数中初始化 +0x310 / +0x314；
//                    原版 CE 脚本 mycall_GetMouseXYinMap 从这两处读取鼠标所指的地图坐标
//   ItemDataHashTable 见 Offsets.cpp 中 GLOBAL_ITEMDATA_TABLE 的说明
#ifndef GAMESTRUCTS_H_INCLUDED_
#define GAMESTRUCTS_H_INCLUDED_

#include <stdint.h>

namespace war3 {
#pragma pack(push, 1)

	struct CPlayerWar3;
	struct CSelectionWar3;
	struct CCameraWar3;

	// 全局游戏对象（GLOBAL_GAMEWAR3 指向的指针）
	struct CGameWar3 {
		void**			vtable;				//0x0
		uint32_t		unk_4;				//0x4
		uint32_t		jassStringId;		//0x8
		uint8_t			unk_C[0x1C];		//0xC
		uint16_t		localPlayerSlot;	//0x28	本地玩家编号（正常游戏时 GetLocalPlayer 返回此值）
		uint16_t		viewPlayerSlot;		//0x2A	回放 / 观察时 GetLocalPlayer 返回此值（1.24E 0x3BC6AF 处判断）
		uint8_t			unk_2C[0x18];		//0x2C
		uint32_t		maxPlayers;			//0x44	地图玩家槽位数
		uint8_t			unk_48[0x04];		//0x48
		uint32_t		activePlayers;		//0x4C	活动玩家数
		uint8_t			unk_50[0x08];		//0x50
		CPlayerWar3*	players[16];		//0x58
	};//sizeof?

	struct CPlayerWar3 {
		void**			vtable;				//0x0
		uint8_t			unk_04[0x2C];		//0x4
		uint16_t		slot;				//0x30
		uint8_t			unk_32[0x02];		//0x32
		CSelectionWar3*	selection;			//0x34
	};//sizeof?

	// 世界画面（大地图）框架
	struct CWorldFrameWar3 {
		void**			vtable;				//0x0	1.24E = 0x6F9536D4
		uint8_t			unk_4[0x198];		//0x4
		CCameraWar3*	camera;				//0x19C
		uint8_t			unk_1A0[0x170];		//0x1A0
		float			mouseWorldX;		//0x310	鼠标所指的地图坐标 X（原版 CE：[MouseXYPointer]+310）
		float			mouseWorldY;		//0x314	鼠标所指的地图坐标 Y（原版 CE：[MouseXYPointer]+314）
	};//sizeof?

	// 游戏界面（GLOBAL_GAMEUI 指向的指针），sizeof = 0x454（1.24E new 0x454）
	// 6328 之前的版本在 0x1B4 之后少 0xC 字节，访问 0x1B4 之后的字段请用 GameUIField()
	struct CGameUI {
		uint8_t			unk_0[0x3BC];		//0x0	CScreenFrame 及各种模式 / 计时器，本工程不访问
		CWorldFrameWar3* world;				//0x3BC	世界画面框架
		void*			minimap;			//0x3C0
		void*			infobar;			//0x3C4
		void*			commandBar;			//0x3C8
		void*			resourceBar;		//0x3CC
	};//sizeof = 0x454

	// Storm 哈希表链表节点：物品数据
	struct ItemDataNode {
		uint32_t		hashKey;			//0x0
		uint8_t			unk_4[0x10];		//0x4
		uint32_t		typeId;				//0x14	物品类型 ID（4字符代码）
	};

	// 物品数据哈希表（TSHashTable<ItemDataNode>），全局 GLOBAL_ITEMDATA_TABLE
	// 遍历方式与游戏自身（1.24E 0x2B9320）相同：
	//     node = firstNode;  while ((int)node > 0) { ...; node = *(node + linkOffset + 4); }
	struct ItemDataHashTable {
		void**			vtable;				//0x0
		int32_t			linkOffset;			//0x4	节点内链表字段的偏移
		void*			lastLink;			//0x8
		ItemDataNode*	firstNode;			//0xC	首节点（<= 0 表示链表为空 / 结束标记）
	};

#pragma pack(pop)
}//namespace war3

#endif // GAMESTRUCTS_H_INCLUDED_
