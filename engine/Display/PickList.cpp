
// PickList.cpp
//
// Copyright (c) 2015 Michael Imamura.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.

#include "../Util/Symbol.h"
#include "SymbolIcon.h"

#include "PickList.h"

using namespace HoverRace::Util;

namespace HoverRace {
namespace Display {

//{{{ PickListItem /////////////////////////////////////////////////////////////

PickListItem::PickListItem(Display &display, const std::string &text,
	uiLayoutFlags_t layoutFlags) :
	SUPER(display, text, layoutFlags)
{
	SetTextAlignment(Alignment::W);
	Init();
}

void PickListItem::Init()
{
	InitIcon(false, false);
	InitIcon(false, true);
	InitIcon(true, false);
	InitIcon(true, true);
}

/**
 * Generate and register the icon for a given combination of states.
 * @param enabled @c true for the enabled state, @c false for disabled.
 * @param checked @c true for the checked state, @c false for unchecked.
 */
void PickListItem::InitIcon(bool enabled, bool checked)
{
	// Using the same symbols as radio buttons for now.

	auto icon = std::make_shared<SymbolIcon>(
		1, 1,  // Size will be set in the superclass in Layout().
		checked ? Symbol::DOT_CIRCLE_O : Symbol::CIRCLE_O,
		enabled ? COLOR_WHITE : 0x7fffffff);
	icon->AttachView(display);

	SetStateIcon(enabled, checked, icon);
}

//}}} PickListItem

//{{{ BasePickList /////////////////////////////////////////////////////////////

BasePickList::BasePickList(Display &display, const Vec2 &size,
	uiLayoutFlags_t layoutFlags) :
	SUPER(display, size, true, layoutFlags),
	listHeight(0)
{
}

bool BasePickList::OnAction()
{
	if (auto focusedChild = GetFocusedChild()) {
		return focusedChild->OnAction();
	}
	return false;
}

bool BasePickList::OnNavigate(const Control::Nav &nav)
{
	if (auto focusedChild = GetFocusedChild()) {
		return focusedChild->OnNavigate(nav);
	}
	return false;
}

void BasePickList::OnChildRequestedFocus(UiViewModel &child)
{
	if (!IsFocused()) {
		RequestFocus();
	}

	if (IsFocused()) {
		// Switch focus to the new child, if possible.
		GetFocusedChild()->DropFocus();
		focusedItem = FindChildIndex(child);

		if (focusedItem && !child.TryFocus()) {
			focusedItem = boost::none;
		}
		if (!focusedItem) {
			// The child that requested focus refused to take the focus, or
			// the child was not one of the filtered items.
			// Either way, this shouldn't happen.
			RelinquishFocus(Control::Nav::NEUTRAL);
		}
	}
}

void BasePickList::OnChildRelinquishedFocus(UiViewModel&,
	const Control::Nav &nav)
{
	using Nav = Control::Nav;

	size_t oldFocusIdx;
	if (auto focusedChild = GetFocusedChild()) {
		oldFocusIdx = *focusedItem;
		focusedChild->DropFocus();
		focusedItem = boost::none;
	}
	else {
		RelinquishFocus(nav);
		return;
	}

	auto dir = nav.AsDigital();
	switch (dir) {
		case Nav::NEUTRAL:
		case Nav::LEFT:
		case Nav::RIGHT:
		case Nav::PREV:
		case Nav::NEXT:
			RelinquishFocus(nav);
			return;

		case Nav::UP:
			if (oldFocusIdx == 0) {
				RelinquishFocus(nav);
				return;
			}
			else {
				focusedItem = oldFocusIdx - 1;
				break;
			}

		case Nav::DOWN:
			if (oldFocusIdx == filteredItems.size() - 1) {
				RelinquishFocus(nav);
				return;
			}
			else {
				focusedItem = oldFocusIdx + 1;
			}
			break;

		default:
			throw UnimplementedExn(boost::str(boost::format(
				"PickList::OnChildRelinquishedFocus(%s)") % nav));
	}

	// All child widgets should be focusable, but check just in case.
	if (!GetFocusedChild()->TryFocus()) {
		focusedItem = boost::none;
		SetFocused(false);
	}
	else {
		SetFocused(true);
	}
}


bool BasePickList::TryFocus(const Control::Nav &nav)
{
	using Nav = Control::Nav;

	if (IsFocused()) return true;
	if (!IsVisible()) return false;
	if (filteredItems.empty()) return false;

	auto dir = nav.AsDigital();
	switch (dir) {
		case Nav::NEUTRAL:
		case Nav::LEFT:
		case Nav::RIGHT:
		case Nav::DOWN:
		case Nav::NEXT:
		case Nav::PREV:
			focusedItem = 0;
			break;

		case Nav::UP:
			focusedItem = filteredItems.size() - 1;
			break;

		default:
			throw UnimplementedExn(boost::str(
				boost::format("PickList::TryFocus: Unhandled: %s") % nav));
	}

	// All child widgets should be focusable, but check just in case.
	if (!GetFocusedChild()->TryFocus()) {
		focusedItem = boost::none;
		return false;
	}

	SetFocused(true);
	return true;
}

void BasePickList::DropFocus()
{
	if (auto focusedChild = GetFocusedChild()) {
		focusedChild->DropFocus();
		focusedItem = boost::none;
	}
	SUPER::DropFocus();
}

void BasePickList::Layout()
{
	double cx = 0;
	double w = GetSize().x;

	ForEachVisibleChild([&](const std::shared_ptr<UiViewModel> &model) {
		auto item = static_cast<PickListItem*>(model.get());

		auto size = item->Measure();
		item->SetSize(w, size.y);
		item->SetPos(0, cx);

		cx += size.y;
	});

	listHeight = cx;
}

//}}} BasePickList

}   ///< namespace Display
}  // namespace HoverRace
