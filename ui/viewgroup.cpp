#include "base/display.h"
#include "base/logging.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

namespace UI {

void ApplyGravity(const Bounds outer, const Margins &margins, float w, float h, int gravity, Bounds &inner) {
	inner.w = w - (margins.left + margins.right);
	inner.h = h - (margins.right + margins.left); 

	switch (gravity & G_HORIZMASK) {
	case G_LEFT: inner.x = outer.x + margins.left; break;
	case G_RIGHT: inner.x = outer.x + outer.w - w - margins.right; break;
	case G_HCENTER: inner.x = outer.x + (outer.w - w) / 2; break;
	}

	switch (gravity & G_VERTMASK) {
	case G_TOP: inner.y = outer.y + margins.top; break;
	case G_BOTTOM: inner.y = outer.y + outer.h - h - margins.bottom; break;
	case G_VCENTER: inner.y = outer.y + (outer.h - h) / 2; break;
	}
}

ViewGroup::~ViewGroup() {
	// Tear down the contents recursively.
	for (auto iter = views_.begin(); iter != views_.end(); ++iter)
		delete *iter;
}

void ViewGroup::Touch(const TouchInput &input) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Touch(input);
	}
}

void ViewGroup::Draw(UIContext &dc) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Draw(dc);
	}
}

void ViewGroup::Update(const InputState &input_state) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Update(input_state);
	}
}

bool ViewGroup::SetFocus() {
	if (!CanBeFocused() && !views_.empty()) {
		for (size_t i = 0; i < views_.size(); i++) {
			if (views_[i]->SetFocus())
				return true;
		}
	}
	return false;
}

bool ViewGroup::SubviewFocused(View *view) {
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view)
			return true;
		if (views_[i]->SubviewFocused(view))
			return true;
	}
	return false;
}

float GetDirectionScore(View *origin, View *destination, FocusDirection direction) {
	// Skip labels and things like that.
	if (!destination->CanBeFocused())
		return 0.0f;

	float dx = destination->GetBounds().centerX() - origin->GetBounds().centerX();
	float dy = destination->GetBounds().centerY() - origin->GetBounds().centerY();

	float distance = sqrtf(dx*dx+dy*dy);
	float dirX = dx / distance;
	float dirY = dy / distance;

	switch (direction) {
	case FOCUS_LEFT:
		if (dirX > 0.0f) return 0.0f;
		if (fabsf(dirY) > fabsf(dirX)) return 0.0f;
		break;
	case FOCUS_UP:
		if (dirY > 0.0f) return 0.0f;
		if (fabsf(dirX) > fabsf(dirY)) return 0.0f;
		break;
	case FOCUS_RIGHT:
		if (dirX < 0.0f) return 0.0f;
		if (fabsf(dirY) > fabsf(dirX)) return 0.0f;
		break;
	case FOCUS_DOWN:
		if (dirY < 0.0f) return 0.0f;
		if (fabsf(dirX) > fabsf(dirY)) return 0.0f;
		break;
	}

	return 100.0f / distance;
}


NeighborResult ViewGroup::FindNeighbor(View *view, FocusDirection direction, NeighborResult result) {
	// First, find the position of the view in the list.
	size_t num = -1;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			num = i;
			break;
		}
	}

	// TODO: Do the cardinal directions right. Now we just map to
	// prev/next.
	
	switch (direction) {
	case FOCUS_PREV:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(0, 0.0f);
		return NeighborResult(views_[(num + views_.size() - 1) % views_.size()], 0.0f);

	case FOCUS_NEXT:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(0, 0.0f);
		return NeighborResult(views_[(num + 1) % views_.size()], 0.0f);

	case FOCUS_UP:
	case FOCUS_LEFT:
	case FOCUS_RIGHT:
	case FOCUS_DOWN:
		{
			// First, try the child views themselves as candidates
			for (size_t i = 0; i < views_.size(); i++) {
				if (views_[i] == view)
					continue;

				float score = GetDirectionScore(view, views_[i], direction);
				if (score > result.score) {
					result.score = score;
					result.view = views_[i];
				}
			}

			// Then go right ahead and see if any of the children contain any better candidates.
			for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
				ViewGroup *vg = dynamic_cast<ViewGroup *>(*iter);
				if (vg)
					result = vg->FindNeighbor(view, direction, result);
			}

			// Boost neighbors with the same parent
			if (num != -1) {
				result.score += 100.0f;
			}

			return result;
		}

	default:
		return result;
	} 
}

void MoveFocus(ViewGroup *root, FocusDirection direction) {
	if (!GetFocusedView()) {
		// Nothing was focused when we got in here. Focus the first non-group in the hierarchy.
		root->SetFocus();
		return;
	}

	NeighborResult neigh(0, 0);
	neigh = root->FindNeighbor(GetFocusedView(), direction, neigh);

	if (neigh.view) {
		neigh.view->SetFocus();

		root->SubviewFocused(neigh.view);

		//if (neigh.parent != 0) {
			// Let scrollviews and similar know that a child has been focused.
			//neigh.parent->SubviewFocused(neigh.view);
		//}
	}
}

void LinearLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);
		return; 
	}

	float sum = 0.0f;
	float maxOther = 0.0f;
	float totalWeight = 0.0f;
	float weightSum = 0.0f;
	float weightZeroSum = 0.0f;

	for (size_t i = 0; i < views_.size(); i++) {
		const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
		const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);
		Margins margins = defaultMargins_;

		if (linLayoutParams) {
			totalWeight += linLayoutParams->weight;
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			views_[i]->Measure(dc, MeasureSpec(UNSPECIFIED), vert - (float)(margins.top + margins.bottom));
		} else if (orientation_ == ORIENT_VERTICAL) {
			views_[i]->Measure(dc, horiz - (float)(margins.left + margins.right), MeasureSpec(UNSPECIFIED));
		}

		float amount;
		if (orientation_ == ORIENT_HORIZONTAL) {
			amount = views_[i]->GetMeasuredWidth() + margins.left + margins.right;
			maxOther = std::max(maxOther, views_[i]->GetMeasuredHeight() + margins.top + margins.bottom);
		} else {
			amount = views_[i]->GetMeasuredHeight() + margins.top + margins.bottom;
			maxOther = std::max(maxOther, views_[i]->GetMeasuredWidth() + margins.left + margins.right);
		}

		sum += amount;
		if (linLayoutParams) {
			if (linLayoutParams->weight == 0.0f)
				weightZeroSum += amount;

			weightSum += linLayoutParams->weight;
		} else {
			weightZeroSum += amount;
		}
	}

	weightZeroSum += spacing_ * (views_.size() - 1);
	
	// Awright, got the sum. Let's take the remaining space after the fixed-size views,
	// and distribute among the weighted ones.
	if (orientation_ == ORIENT_HORIZONTAL) {
		MeasureBySpec(layoutParams_->width, weightZeroSum, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, maxOther, vert, &measuredHeight_);

		float unit = (measuredWidth_ - weightZeroSum) / weightSum;
		// Redistribute the stretchy ones! and remeasure the children!
		for (size_t i = 0; i < views_.size(); i++) {
			const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
			const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

			if (linLayoutParams && linLayoutParams->weight > 0.0)
				views_[i]->Measure(dc, MeasureSpec(EXACTLY, unit * linLayoutParams->weight), vert);
		}
	} else {
		MeasureBySpec(layoutParams_->height, weightZeroSum, vert, &measuredHeight_);
		MeasureBySpec(layoutParams_->width, maxOther, horiz, &measuredWidth_);

		float unit = (measuredHeight_ - weightZeroSum) / weightSum;

		// Redistribute! and remeasure children!
		for (size_t i = 0; i < views_.size(); i++) {
			const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
			const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

			if (linLayoutParams && linLayoutParams->weight)
				views_[i]->Measure(dc, horiz, MeasureSpec(EXACTLY, unit * linLayoutParams->weight));
		}
	}
}

// TODO: Stretch and squeeze!
// weight != 0 = fill remaining space.
void LinearLayout::Layout() {
	const Bounds &bounds = bounds_;

	Bounds itemBounds;
	float pos;
	
	if (orientation_ == ORIENT_HORIZONTAL) {
		pos = bounds.x;
		itemBounds.y = bounds.y;
		itemBounds.h = measuredHeight_;
	} else {
		pos = bounds.y;
		itemBounds.x = bounds.x;
		itemBounds.w = measuredWidth_;
	}

	for (size_t i = 0; i < views_.size(); i++) {
		const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
		const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

		Gravity gravity = G_TOPLEFT; 
		Margins margins = defaultMargins_;
		if (linLayoutParams) {
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
			gravity = linLayoutParams->gravity;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			itemBounds.x = pos;
			itemBounds.w = views_[i]->GetMeasuredWidth() + margins.left + margins.right;
		} else {
			itemBounds.y = pos;
			itemBounds.h = views_[i]->GetMeasuredHeight() + margins.top + margins.bottom;
		}

		Bounds innerBounds;
		ApplyGravity(itemBounds, margins,
			views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(),
			gravity, innerBounds);

		views_[i]->SetBounds(innerBounds);
		views_[i]->Layout();

		pos += spacing_ + (orientation_ == ORIENT_HORIZONTAL ? itemBounds.w : itemBounds.h);
	}
}

void FrameLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);
		return; 
	}

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->Measure(dc, horiz, vert);
	}
}

void FrameLayout::Layout() {

}

void ScrollView::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	// Respect margins
	Margins margins;
	const LinearLayoutParams *params = dynamic_cast<const LinearLayoutParams*>(views_[0]->GetLayoutParams());
	if (params) {
		margins = params->margins;
	}

	// The scroll view itself simply obeys its parent.
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);

	if (orientation_ == ORIENT_HORIZONTAL) {
		views_[0]->Measure(dc, MeasureSpec(UNSPECIFIED), vert - (margins.top + margins.bottom));
	} else {
		views_[0]->Measure(dc, horiz - (margins.left + margins.right), MeasureSpec(UNSPECIFIED));
	}
}

void ScrollView::Layout() {
	Bounds scrolled;

	// Respect margins
	Margins margins;
	const LinearLayoutParams *params = dynamic_cast<const LinearLayoutParams*>(views_[0]->GetLayoutParams());
	if (params) {
		margins = params->margins;
	}

	scrolled.w = views_[0]->GetMeasuredWidth() - (margins.left + margins.right);
	scrolled.h = views_[0]->GetMeasuredHeight() - (margins.top + margins.bottom);

	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		scrolled.x = bounds_.x - scrollPos_;
		scrolled.y = bounds_.y + margins.top;
		break;
	case ORIENT_VERTICAL:
		scrolled.x = bounds_.x + margins.left;
		scrolled.y = bounds_.y - scrollPos_;
		break;
	}

	views_[0]->SetBounds(scrolled);
	views_[0]->Layout();
}

void ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && input.id == 0) {
		scrollStart_ = scrollPos_;
	}
	
	TouchInput input2 = gesture_.Update(input, bounds_);

	if (gesture_.IsGestureActive(GESTURE_DRAG_VERTICAL)) {
		float info[4];
		gesture_.GetGestureInfo(GESTURE_DRAG_VERTICAL, info);
		scrollPos_ = scrollStart_ - info[0];
	}
	
	if (!(input.flags & TOUCH_DOWN) || bounds_.Contains(input.x, input.y))
		ViewGroup::Touch(input2);
	
	// Clamp scrollPos_. TODO: flinging, bouncing, etc.
	if (scrollPos_ < 0.0f) {
		scrollPos_ = 0.0f;
	}
	float childHeight = views_[0]->GetBounds().h;
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);
	if (scrollPos_ > scrollMax) {
		scrollPos_ = scrollMax;
	}
}

void ScrollView::Draw(UIContext &dc) {
	dc.PushScissor(bounds_);
	views_[0]->Draw(dc);
	dc.PopScissor();
}

bool ScrollView::SubviewFocused(View *view) {
	if (!ViewGroup::SubviewFocused(view))
		return false;

	const Bounds &vBounds = view->GetBounds();

	// Scroll so that the focused view is visible.
	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		if (vBounds.x2() > bounds_.x2()) {
			ScrollTo(scrollPos_ + vBounds.x2() - bounds_.x2());
		}
		if (vBounds.x < bounds_.x) {
			ScrollTo(scrollPos_ + (vBounds.x - bounds_.x));
		}
		break;
	case ORIENT_VERTICAL:
		if (vBounds.y2() > bounds_.y2()) {
			ScrollTo(scrollPos_ + vBounds.y2() - bounds_.y2());
		}
		if (vBounds.y < bounds_.y) {
			ScrollTo(scrollPos_ + (vBounds.y - bounds_.y - 30));
		}
		break;
	}

	return true;
}

void ScrollView::ScrollTo(float newScrollPos) {
	scrollTarget_ = newScrollPos;
	scrollToTarget_ = true;
}

void ScrollView::Update(const InputState &input_state) {
	ViewGroup::Update(input_state);
	if (scrollToTarget_) {
		if (fabsf(scrollTarget_ - scrollPos_) < 0.5f) {
			scrollPos_ = scrollTarget_;
			scrollToTarget_ = false;
		} else {
			scrollPos_ += (scrollTarget_ - scrollPos_) * 0.3f;
		}
	}
}

void GridLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureSpecType measureType = settings_.fillCells ? EXACTLY : AT_MOST;

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->Measure(dc, MeasureSpec(measureType, settings_.columnWidth), MeasureSpec(measureType, settings_.rowHeight));
	}

	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);

	// Okay, got the width we are supposed to adjust to. Now we can calculate the number of columns.
	int numColumns = (measuredWidth_ - settings_.spacing) / (settings_.columnWidth + settings_.spacing);
	int numRows = (views_.size() + (numColumns - 1)) / numColumns;

	float estimatedHeight = settings_.rowHeight * numRows;

	MeasureBySpec(layoutParams_->height, estimatedHeight, vert, &measuredHeight_);
}

void AnchorLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, horiz, &measuredHeight_);

	for (size_t i = 0; i < views_.size(); i++) {
		Size width = WRAP_CONTENT;
		Size height = WRAP_CONTENT;

		MeasureSpec specW(UNSPECIFIED, 0.0f);
		MeasureSpec specH(UNSPECIFIED, 0.0f);

		const AnchorLayoutParams *params = dynamic_cast<const AnchorLayoutParams *>(views_[i]->GetLayoutParams());
		if (params) {
			width = params->width;
			height = params->height;

			if (params->left >= 0 && params->right >= 0) 	{
				width = measuredWidth_ - params->left - params->right;
				specW = MeasureSpec(EXACTLY, width);
			}
			if (params->top >= 0 && params->bottom >= 0) 	{
				height = measuredHeight_ - params->top - params->bottom;
				specH = MeasureSpec(EXACTLY, height);
			}
		}

		views_[i]->Measure(dc, specW, specH);
	}
}

void AnchorLayout::Layout() {
	for (size_t i = 0; i < views_.size(); i++) {
		const AnchorLayoutParams *params = dynamic_cast<const AnchorLayoutParams *>(views_[i]->GetLayoutParams());

		Bounds vBounds;
		vBounds.w = views_[i]->GetMeasuredWidth();
		vBounds.h = views_[i]->GetMeasuredHeight();

		float left = 0, top = 0, right = 0, bottom = 0;

		if (params) {
			left = params->left;
			top = params->top;
			right = params->right;
			bottom = params->bottom;
		}

		if (left >= 0) vBounds.x = bounds_.x + left;
		if (top >= 0)	vBounds.y = bounds_.y + top;
		if (right >= 0) vBounds.x = bounds_.x2() - right - vBounds.w;
		if (bottom >= 0) vBounds.y = bounds_.y2() - bottom - vBounds.y;

		views_[i]->SetBounds(vBounds);
	}
}

void GridLayout::Layout() {
	int y = 0;
	int x = 0;
	for (size_t i = 0; i < views_.size(); i++) {
		Bounds itemBounds, innerBounds;

		itemBounds.x = bounds_.x + x;
		itemBounds.y = bounds_.y + y;
		itemBounds.w = settings_.columnWidth;
		itemBounds.h = settings_.rowHeight;

		ApplyGravity(itemBounds, Margins(0.0f),
			views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(),
			G_HCENTER | G_VCENTER, innerBounds);

		views_[i]->SetBounds(innerBounds);
		views_[i]->Layout();

		x += itemBounds.w;
		if (x >= bounds_.w) {
			x = 0;
			y += itemBounds.h + settings_.spacing;
		} else {
			x += settings_.spacing;
		}
	}
}

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root) {
	Bounds rootBounds;
	rootBounds.x = 0;
	rootBounds.y = 0;
	rootBounds.w = dp_xres;
	rootBounds.h = dp_yres;

	MeasureSpec horiz(EXACTLY, rootBounds.w);
	MeasureSpec vert(EXACTLY, rootBounds.h);

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	root->SetBounds(rootBounds);
	root->Layout();
}


void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root) {
	if (input_state.pad_buttons_down & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN))
	{
		EnableFocusMovement(true);
		if (!GetFocusedView()) {
			root->SetFocus();
			root->SubviewFocused(GetFocusedView());
		} else {
			if (input_state.pad_buttons_down & PAD_BUTTON_RIGHT)
				MoveFocus(root, FOCUS_RIGHT);
			if (input_state.pad_buttons_down & PAD_BUTTON_UP)
				MoveFocus(root, FOCUS_UP);
			if (input_state.pad_buttons_down & PAD_BUTTON_LEFT)
				MoveFocus(root, FOCUS_LEFT);
			if (input_state.pad_buttons_down & PAD_BUTTON_DOWN)
				MoveFocus(root, FOCUS_DOWN);
		}
	}
	if (input_state.pointer_down[0])
		EnableFocusMovement(false);

	root->Update(input_state);
}

}  // namespace UI