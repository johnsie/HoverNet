// The track editor's state and what a person can do to it, with no window and no drawing: selection, dragging, placing, deleting, undo and redo, new, open
// and save. The interface (EditorMain.cpp) is a thin layer over this, so what the tests prove here is what the editor does on Windows, Linux and in the
// browser alike (docs/roadmap-track-editor.md, parity rule 5: one implementation for every platform).
#pragma once

#include "TrackEdit.h"
#include "TrackSource.h"
#include "TrackTemplates.h"

#include <string>
#include <vector>

namespace HoverNet
{
struct Selection
{
    enum Kind { None, Room, Start, Element, Corner } kind = None;
    int index = -1;    // the room, start or element; for a corner, the room
    int corner = -1;   // a corner's number in its room
    bool operator==(const Selection& o) const { return kind == o.kind && index == o.index && corner == o.corner; }
};

class EditorSession
{
public:
    EditorSession();

    TrackModel model;
    std::string path;                    // where the track is saved ("" until the first save)
    bool dirty = false;                  // changed since it was saved or opened
    Selection selection;
    std::string status;                  // the last thing the editor wants to tell the person, in plain words
    std::vector<std::string> problems;   // what the editor can see is wrong without compiling (TrackModel::Problems)

    // files
    bool NewLoop(const NewTrackOptions& options);
    bool NewStartRoom(const std::string& name);   // one start room to build the road from
    bool Open(const std::string& filename);
    bool Save();                         // to `path`
    bool SaveAs(const std::string& filename);
    std::string Text() const { return model.ToText(); }

    // history: every change is one step. Opening or starting a track clears it.
    bool CanUndo() const { return !mUndo.empty(); }
    bool CanRedo() const { return !mRedo.empty(); }
    bool Undo();
    bool Redo();

    // pointer actions, in plan coordinates (metres, y up). `radius` is how close counts as a hit, in metres (the interface passes a few pixels' worth).
    Selection HitTest(double x, double y, double radius) const;
    void Select(const Selection& s) { selection = s; }
    bool BeginDrag(double x, double y, double radius);   // selects what is under the pointer and starts moving it when it can move
    bool Drag(double x, double y);                       // false when the move was refused (the thing stays where it was; status says why)
    void EndDrag();                                      // an unmoved drag leaves no undo step
    bool Dragging() const { return mDragging; }

    // placing and removing
    bool PlaceElement(int elementType, double x, double y);
    bool PlaceStart(double x, double y);
    bool DeleteSelected();                // an element, a start, or a room (rooms with starts or elements in them are refused, with the reason)
    // building the road: add a room beyond the nearest edge to the point (within `radius` metres), `length` long and turned `turn` degrees (left is positive)
    bool ExtendRoadAt(double x, double y, double radius, double length, double turn);
    // the polygon ExtendRoadAt would add there, for drawing a ghost; false when nothing could be added (why is in `reason`)
    bool PreviewExtend(double x, double y, double radius, double length, double turn, std::vector<WallPoint>& polygon, int& roomIndex, int& wallIndex, std::string& reason) const;

    // property edits on the selection (each one undo step)
    bool SetHeight(SurfaceSlot slot, double height);
    bool SetSurface(SurfaceSlot slot, int surfaceId);
    bool SetOrientationOfSelected(double degrees);

    void Refresh();                      // recompute problems and the dirty mark after a change

private:
    void Checkpoint();                   // remember the track before a change
    void Changed(const std::string& message);
    std::vector<TrackModel> mUndo, mRedo;
    bool mDragging = false, mMoved = false;
    TrackModel mBeforeDrag;
};
}  // namespace HoverNet
