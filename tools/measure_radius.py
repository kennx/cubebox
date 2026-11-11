#!/usr/bin/env python3
"""measure_radius.py — 从白屏照片拟合 AMOLED 四角圆角遮罩半径

用法：
    python3 tools/measure_radius.py photo.jpg [-o diag.png] [--thr 200]

依赖：numpy、Pillow。照片自动降采样到长边 2400px（连通域 BFS 是纯 Python
实现，48MP 原片会内存/时间失控；输出经单应归一化到屏幕坐标，不受降采样影响）。
前提：设备显示 app_radius 页 0（全白屏），手机正对屏幕拍摄。
流程：
    1. 高阈值分割屏幕亮区（发光白屏 vs 黑边框；白色外壳漫反射通常低于阈值）
    2. 取最大连通域为屏幕 mask，提取边界点（mask 减腐蚀）
    3. 边界点按方位分四组（各取该向极值带、去掉角部中段），RANSAC 拟合
       四条直边，交点即四个"未圆角化"理论角点
    4. 四角点单应性校正到 480×480 屏幕坐标
    5. 角方块（120×120）内、距两直边 >3px 的边界点即圆角弧点，
       Kasa 鲁棒圆拟合得每角半径
诊断：-o 输出校正后边界点与拟合圆叠加图。
"""
import argparse
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

SCREEN = 480.0
CORNER_BOX = 120      # 角方块边长（屏幕 px）
EDGE_OFF = 3          # 距直边小于此值的点视为直边点剔除


def largest_component(mask):
    lab = np.zeros(mask.shape, np.int32)
    best_n, best_id, cur = 0, 0, 0
    ys, xs = np.where(mask)
    H, W = mask.shape
    for sy, sx in zip(ys, xs):
        if lab[sy, sx]:
            continue
        cur += 1
        q = deque([(sy, sx)])
        lab[sy, sx] = cur
        n = 0
        while q:
            y, x = q.popleft()
            n += 1
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < H and 0 <= nx < W and mask[ny, nx] and not lab[ny, nx]:
                    lab[ny, nx] = cur
                    q.append((ny, nx))
        if n > best_n:
            best_n, best_id = n, cur
    return lab == best_id, best_n


def boundary(mask):
    """mask 边界点：mask 中真、四邻域至少一个假的点。返回 (N,2) xy。"""
    interior = mask.copy()
    interior[1:, :] &= mask[:-1, :]
    interior[:-1, :] &= mask[1:, :]
    interior[:, 1:] &= mask[:, :-1]
    interior[:, :-1] &= mask[:, 1:]
    interior[0, :] = False
    interior[-1, :] = False
    interior[:, 0] = False
    interior[:, -1] = False
    ys, xs = np.where(mask & ~interior)
    return np.stack([xs, ys], 1).astype(float)


def fit_line_ransac(pts, iters=500, tol=2.0):
    rng = np.random.default_rng(7)
    best_in = None
    N = len(pts)
    for _ in range(iters):
        i, j = rng.integers(N, size=2)
        if i == j:
            continue
        (x1, y1), (x2, y2) = pts[i], pts[j]
        a, b = y2 - y1, x1 - x2
        norm = np.hypot(a, b)
        if norm < 1e-6:
            continue
        a, b, c = a / norm, b / norm, -(a * x1 + b * y1) / norm
        d = np.abs(pts @ np.array([a, b]) + c)
        inl = d < tol
        if best_in is None or inl.sum() > best_in.sum():
            best_in = inl
    if best_in is None:
        sys.exit('RANSAC 退化：边点过于集中或共线，无法拟合直线')
    P = pts[best_in]
    cen = P.mean(0)
    _, _, vt = np.linalg.svd(P - cen, full_matrices=False)
    d = vt[0]
    n = np.array([-d[1], d[0]])
    c = -n @ cen
    return (n[0], n[1], c)


def refit_line(pts, prev):
    """带内点的最小二乘重拟合（粗差 >4px 剔除）；点太少时沿用旧线。"""
    a, b, c = prev
    d = np.abs(pts @ np.array([a, b]) + c)
    P = pts[d < 4]
    if len(P) < 30:
        return prev
    cen = P.mean(0)
    _, _, vt = np.linalg.svd(P - cen, full_matrices=False)
    dirv = vt[0]
    n = np.array([-dirv[1], dirv[0]])
    if n @ np.array([a, b]) < 0:  # 保持法向朝向一致
        n = -n
    return (n[0], n[1], -n @ cen)


def line_intersect(l1, l2):
    a1, b1, c1 = l1
    a2, b2, c2 = l2
    det = a1 * b2 - a2 * b1
    if abs(det) < 1e-9:
        return None
    return np.array([(b1 * c2 - b2 * c1) / det, (a2 * c1 - a1 * c2) / det])


def homography(src, dst):
    A = []
    for (x, y), (u, v) in zip(src, dst):
        A.append([-x, -y, -1, 0, 0, 0, u * x, u * y, u])
        A.append([0, 0, 0, -x, -y, -1, v * x, v * y, v])
    _, _, Vt = np.linalg.svd(np.array(A, float))
    H = Vt[-1].reshape(3, 3)
    return H / H[2, 2]


def apply_h(H, P):
    Ph = np.hstack([P, np.ones((len(P), 1))]) @ H.T
    return Ph[:, :2] / Ph[:, 2:3]


def fit_circle(pts):
    A = np.hstack([2 * pts, np.ones((len(pts), 1))])
    b = (pts ** 2).sum(1)
    sol, *_ = np.linalg.lstsq(A, b, rcond=None)
    cx, cy = sol[0], sol[1]
    r = np.sqrt(max(sol[2] + cx * cx + cy * cy, 1e-9))
    return cx, cy, r


def robust_circle(pts, iters=6, sigma_k=2.0):
    model = fit_circle(pts)
    used = pts
    for _ in range(iters):
        cx, cy, r = model
        d = np.abs(np.hypot(pts[:, 0] - cx, pts[:, 1] - cy) - r)
        keep = d < max(2.0, d.mean() + sigma_k * d.std())
        if keep.sum() < 15:
            break
        model = fit_circle(pts[keep])
        used = pts[keep]
    return model, used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('photo')
    ap.add_argument('-o', '--out')
    ap.add_argument('--thr', type=int, default=200)
    args = ap.parse_args()

    im = Image.open(args.photo).convert('L')
    im.thumbnail((2400, 2400))  # 纯 Python BFS 降内存/耗时；输出为屏幕坐标不受影响
    a = np.asarray(im).astype(np.float32)
    print(f'photo: {im.size[0]}x{im.size[1]}')

    mask, npx = largest_component(a > args.thr)
    print(f'screen mask: {npx} px')
    if npx < 5000:
        sys.exit('亮区过小：检查照片/阈值（--thr）')
    B = boundary(mask)
    print(f'boundary points: {len(B)}')

    x0, x1 = B[:, 0].min(), B[:, 0].max()
    y0, y1 = B[:, 1].min(), B[:, 1].max()
    w, h = x1 - x0, y1 - y0
    midx0, midx1 = x0 + 0.25 * w, x1 - 0.25 * w
    midy0, midy1 = y0 + 0.25 * h, y1 - 0.25 * h
    band = 0.03  # 初始极值带厚度
    sets = {
        'top':    B[(B[:, 1] < y0 + band * h) & (B[:, 0] > midx0) & (B[:, 0] < midx1)],
        'bottom': B[(B[:, 1] > y1 - band * h) & (B[:, 0] > midx0) & (B[:, 0] < midx1)],
        'left':   B[(B[:, 0] < x0 + band * w) & (B[:, 1] > midy0) & (B[:, 1] < midy1)],
        'right':  B[(B[:, 0] > x1 - band * w) & (B[:, 1] > midy0) & (B[:, 1] < midy1)],
    }
    lines = {}
    for name, S in sets.items():
        if len(S) < 30:
            sys.exit(f'{name} 边点不足（{len(S)}）')
        lines[name] = fit_line_ransac(S)

    # EM 迭代吸收整条边（透视下极值带只覆盖倾斜边的一段）：
    # 每个边界点按到 4 线最近距离归类（<15px），重拟合，收敛即止
    Ln = np.array([lines[k] for k in ('top', 'bottom', 'left', 'right')])
    for it in range(12):
        dist = np.abs(B @ Ln[:, :2].T + Ln[:, 2])  # (N,4)
        assign = dist.argmin(1)
        ok = dist.min(1) < 15
        new_lines = []
        for k in range(4):
            P = B[ok & (assign == k)]
            P = P[np.abs(P @ Ln[k, :2] + Ln[k, 2]) < 4]  # 剔粗差
            new_lines.append(refit_line(P, Ln[k]))
        new = np.array(new_lines)
        if np.abs(new - Ln).max() < 1e-6:
            break
        Ln = new
    for k, name in enumerate(('top', 'bottom', 'left', 'right')):
        lines[name] = tuple(Ln[k])
        print(f'{name} line ok (EM iter {it}, inliers {int(ok.sum())})')

    corners = {
        'TL': line_intersect(lines['top'], lines['left']),
        'TR': line_intersect(lines['top'], lines['right']),
        'BR': line_intersect(lines['bottom'], lines['right']),
        'BL': line_intersect(lines['bottom'], lines['left']),
    }
    for k, v in corners.items():
        if v is None:
            sys.exit(f'{k} 角点求解失败（对边近平行）')
    print('corners:', {k: np.round(v, 1).tolist() for k, v in corners.items()})

    src = np.array([corners['TL'], corners['TR'], corners['BR'], corners['BL']])
    dst = np.array([[0, 0], [SCREEN, 0], [SCREEN, SCREEN], [0, SCREEN]], float)
    H = homography(src, dst)
    sb = apply_h(H, B)  # 屏幕坐标边界点

    results, diag_pts = {}, {}
    for name, quad in (
        ('TL', lambda p: (p[:, 0] < CORNER_BOX) & (p[:, 1] < CORNER_BOX)),
        ('TR', lambda p: (p[:, 0] > SCREEN - CORNER_BOX) & (p[:, 1] < CORNER_BOX)),
        ('BR', lambda p: (p[:, 0] > SCREEN - CORNER_BOX) & (p[:, 1] > SCREEN - CORNER_BOX)),
        ('BL', lambda p: (p[:, 0] < CORNER_BOX) & (p[:, 1] > SCREEN - CORNER_BOX)),
    ):
        q = sb[quad(sb)]
        # 剔除直边点：距两条直边（x/y = 0 或 480）小于 EDGE_OFF 的点
        near_edge = ((q[:, 0] < EDGE_OFF) | (q[:, 0] > SCREEN - EDGE_OFF) |
                     (q[:, 1] < EDGE_OFF) | (q[:, 1] > SCREEN - EDGE_OFF))
        arc = q[~near_edge]
        if len(arc) < 30:
            print(f'{name}: 弧点不足（{len(arc)}）')
            continue
        (cx, cy, r), used = robust_circle(arc)
        results[name] = (cx, cy, r)
        diag_pts[name] = used
        # 自检：圆心应接近 (r,r) 形态（角向内 r）
        print(f'{name}: r = {r:6.1f}px  center = ({cx:7.1f}, {cy:7.1f})  '
              f'center-dist-from-corner = ({cx if name[1]=="L" else SCREEN-cx:6.1f}, '
              f'{cy if name[0]=="T" else SCREEN-cy:6.1f})  n={len(used)}')

    if results:
        rs = [v[2] for v in results.values()]
        print(f'\n四角半径: ' + '  '.join(f'{k}={v[2]:.1f}' for k, v in results.items()))
        print(f'汇总: min {min(rs):.1f} / max {max(rs):.1f} / mean {np.mean(rs):.1f}px '
              f'（480px 的 {np.mean(rs) / SCREEN * 100:.1f}%）')

    if args.out:
        diag = Image.new('L', (int(SCREEN), int(SCREEN)), 0)
        dr = ImageDraw.Draw(diag)
        s = sb[(sb[:, 0] >= 0) & (sb[:, 0] < SCREEN) & (sb[:, 1] >= 0) & (sb[:, 1] < SCREEN)]
        for x, y in s[:: max(1, len(s) // 30000)]:
            dr.point((int(x), int(y)), fill=90)
        for name, used in diag_pts.items():
            cx, cy, r = results[name]
            dr.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255)
            for x, y in used[:: max(1, len(used) // 1000)]:
                dr.point((int(x), int(y)), fill=255)
        diag.save(args.out)
        print(f'diag → {args.out}')


if __name__ == '__main__':
    main()
