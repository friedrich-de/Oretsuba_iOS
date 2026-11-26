#include "gfx/surface.h"

#include <algorithm>

#include "gfx/blend.h"
#include "gfx/transition.h"

namespace ore {

uint64_t g_gfx_generation = 1;

// ---------------------------------------------------------------- Compositor

void Compositor::draw_background(Image &target) {
	if (no_bg) return;
	const Rect full = target.rect();
	if (bg_cache) {
		copy_rect(target, 0, 0, *bg_cache, Rect(0, 0, bg_cache->width(), bg_cache->height()));
	} else if (bg) {
		// opaque copy of the visible window of the background; parts outside are left as they were
		copy_rect(target, 0, 0, *bg, Rect(bg_x, bg_y, bg_x + full.w(), bg_y + full.h()));
	} else {
		fill_rect(target, full, target.bpp() == 8 ? (bg_color & 0xff) : (bg_color & 0xffffff));
	}
}

void Compositor::draw_list(Image &target, LayerList *l, int filter_mode, int filter_tag) {
	if (!l) return;
	const Rect full = target.rect();
	auto &n = l->nodes();
	for (size_t i = n.size(); i-- > 0;) {
		Layer *ly = n[i].layer;
		if (!ly->visible || ly->opacity <= 0) continue;
		if (filter_mode == 1 && ly->tag != filter_tag) continue;
		if (filter_mode == 2 && ly->tag == filter_tag) continue;
		ly->draw(target, full);
	}
}

void Compositor::draw_overlay(Image &target) {
	if (!hide_overlay) draw_list(target, overlay);
}

void Compositor::compose(Image &target, int filter_mode, int filter_tag, bool with_overlay) {
	draw_background(target);
	if (!hide_main) draw_list(target, &main, filter_mode, filter_tag);
	if (with_overlay && !hide_overlay) draw_list(target, overlay, filter_mode, filter_tag);
}

void Compositor::swap_background(Compositor &o) {
	std::swap(bg, o.bg);
	std::swap(bg_handle, o.bg_handle);
	std::swap(bg_x, o.bg_x);
	std::swap(bg_y, o.bg_y);
	std::swap(bg_color, o.bg_color);
	std::swap(bg_cache, o.bg_cache);
	std::swap(rot_cx, o.rot_cx);
	std::swap(rot_cy, o.rot_cy);
	gfx_touch();
}

// ---------------------------------------------------------------- Surface

Surface::Surface(int type, int w, int h, int bpp, int mode)
    : type_(type), image_(std::make_shared<Image>(w, h, bpp)), overlay_list_(nullptr) {
	if (mode != 0) {
		comp_ = std::make_unique<Compositor>();
		use_compositor = true;
		if (mode == 2 && type != 10) {
			has_overlay_ = true;
			overlay_list_.set_owner(comp_.get());
			comp_->overlay = &overlay_list_;
		}
	}
}

Surface::~Surface() {
	if (linked && linked->linked == this) linked->linked = nullptr;
	// layers detach through their list destructors
}

Image &Surface::current_image() {
	return transition && work ? *work : *image_;
}

void Surface::update(int64_t now) {
	if (transition && linked) {
		draw_transition(now);
		return;
	}
	if (use_compositor && comp_ && !comp_->disabled && (composed_gen_ != g_gfx_generation || dirty_)) {
		comp_->compose(*image_);
		composed_gen_ = g_gfx_generation;
		dirty_ = false;
	}
}

// One frame of a running transition, into the work image. What the old and
// the new picture are depends on the mode (the original has one drawing
// routine per mode; docs/re/30_effects.md 2.2):
//   0  both page images as they lie; nothing is composed, so a picture the
//      script copied into the page (N_148) is the old picture
//   1  both pages composed anew for every frame, without their overlay lists,
//      so layers on them keep moving
//   2  only the two backgrounds; the front page's layers are drawn over the
//      result
// A page that composes itself then gets its overlay list (the text window and
// the name box) drawn over the frame: those stay above the transition.
void Surface::draw_transition(int64_t now) {
	const bool live = use_compositor && comp_ && !comp_->disabled;
	Compositor *back = linked->use_compositor && linked->comp_ && !linked->comp_->disabled ? linked->comp_.get() : nullptr;
	if (live && trans_mode == 1) {
		comp_->compose(*image_, 0, 0, false);
		if (back) back->compose(linked->image(), 0, 0, false);
	} else if (live && trans_mode == 2) {
		comp_->draw_background(*image_);
		if (back) back->draw_background(linked->image());
	}
	if (transition->start < 0) transition->start = now;
	if (transition->render(now, *image_, linked->image(), *work)) transition->done = true;
	if (live) {
		if (trans_mode == 2 && !comp_->hide_main) comp_->draw_list(*work, &comp_->main);
		comp_->draw_overlay(*work);
	}
	gfx_touch();
}

void Surface::swap_compositors(Surface &other) {
	if (!comp_ || !other.comp_) return;
	// exchange main lists and backgrounds; overlay lists stay with their pages
	LayerList::swap_contents(comp_->main, other.comp_->main);
	comp_->swap_background(*other.comp_);
	std::swap(comp_->no_bg, other.comp_->no_bg);
	std::swap(comp_->hide_main, other.comp_->hide_main);
	gfx_touch();
}

bool Surface::link(Surface *other) {
	if (!other) {
		linked = nullptr;
		return true;
	}
	if (other->image_->width() != image_->width() || other->image_->height() != image_->height() ||
	    other->image_->bpp() != image_->bpp())
		return false;
	linked = other;
	return true;
}

int Surface::start_transition(std::unique_ptr<Transition> t, int mode, bool apply) {
	if (!comp_) return 1;
	if (!linked) return 2;
	if (transition) end_transition(trans_apply);
	transition = std::move(t);
	trans_mode = mode;
	trans_apply = apply;
	if (!work || work->width() != image_->width() || work->height() != image_->height())
		work = std::make_shared<Image>(image_->width(), image_->height(), image_->bpp());
	gfx_touch();
	return 0;
}

void Surface::end_transition(bool apply) {
	if (!transition) return;
	transition.reset();
	if (apply && linked) {
		if (trans_mode == 1) swap_compositors(*linked);
		if (trans_mode == 2) comp_->swap_background(*linked->comp_);
	}
	invalidate();
}

// ---------------------------------------------------------------- View

View::View(Surface *s, const Rect *r, int nx, int ny) : surface(s), x(nx), y(ny) {
	src = r ? *r : s->image().rect();
}

}  // namespace ore
