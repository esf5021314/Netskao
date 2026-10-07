// 模块说明：JASS 原生函数包装的辅助实现，见 Jass.h
#include "stdafx.h"
#include "Jass.h"

namespace jass {

	bool NativesComplete(int* missing) {
		// 版本在进程生命周期内不会变化，结果只计算一次
		static int cached = -1;          // -1 未计算，0 不完整，1 完整
		static int cachedMissing = 0;
		if (cached < 0) {
			cached = 1;
			for (int i = 0; i < NATIVE_ID_COUNT; ++i) {
				if (!Offset(NATIVE_ID_FIRST + i)) {
					cached = 0;
					cachedMissing = NATIVE_ID_FIRST + i;
					break;
				}
			}
		}
		if (missing) *missing = cachedMissing;
		return cached == 1;
	}

}//namespace jass
