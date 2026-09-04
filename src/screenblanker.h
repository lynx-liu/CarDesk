#ifndef SCREENBLANKER_H
#define SCREENBLANKER_H

/** 时钟/关屏是否仍应压住媒体（含转向暂挂期间） */
bool screenBlankerHoldsMedia();

/** 关屏且未因转向/倒车暂亮：自动亮度等不得改背光 */
bool screenBlankerKeepsBacklightOff();

/** 转向/倒车弹出前：收起关屏遮罩；背光暂不恢复，等行车窗上屏后再亮 */
void screenBlankerSuspendForAutomotive();

/** 行车影像已显示：再恢复背光，避免关屏→转向之间闪时钟/主界面 */
void screenBlankerPresentAutomotiveDisplay();

/** 转向/倒车结束后：恢复时钟或关屏，且不恢复音乐/收音机 */
void screenBlankerResumeAfterAutomotive();

#endif // SCREENBLANKER_H
