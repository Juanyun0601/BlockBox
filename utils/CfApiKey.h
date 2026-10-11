#ifndef CF_API_KEY_H
#define CF_API_KEY_H

// ============================================================================
// CurseForge API Key —— 本地密钥文件（勿推送）
//
// 使用方法：
//   1. 把你的真实 key 填到下面 BLOCKBOX_CF_API_KEY 的引号里
//      （CurseForge 后台申请，形如 $2a$10$... 的一串字符）
//   2. 执行一次：
//        git update-index --skip-worktree utils/CfApiKey.h
//      之后本地改动会被 git 忽略，一键上传不会把它带进仓库。
//      （utils/AuthManager.cpp 采用同一机制）
//
// 防泄露链路：
//   - skip-worktree：本地真实内容不进 git add / commit
//   - pre-push 钩子：即使误提交，$2a$ 形式的 key 也会被拦截推送
//
// 注意：填入真实 key 后，本地编译出的 exe/apk 内含该 key（字符串可被提取），
//       此类构建仅限自用，不要作为发布包分发。
//
// 优先级：设置界面填写的 key（curseforge_api_key）> 本文件的值 > 空（走 MCIM 镜像）
// ============================================================================

#define BLOCKBOX_CF_API_KEY ""

#endif // CF_API_KEY_H
