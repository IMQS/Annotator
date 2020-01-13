import { Vec2, Rect, Polygon } from './geom';
import { ImageLabelSet, LabelRegion, Dimension, DimensionType } from './label';

enum State {
	None, // Ready for action
	Frozen, // All mouse input ignored
	NewPolygon, // Busy drawing a new polygon (or new rectangle)
	DragVertex, // Busy dragging a vertex of a polygon or rectangle
}

// Closest line segment, or closest vertex
export class HitTest {
	region: LabelRegion;
	idx: number; // index in region.polygon.vx, of closest vertex, or segment
	distance: number; // distance to idxV

	constructor(region: LabelRegion, idx: number, distance: number) {
		this.region = region;
		this.idx = idx;
		this.distance = distance;
	}
}

// Popup is used to represent a hotspot on the image that can be clicked on,
// and then we show a dropdown. That dropdown can be used to change the
// label of a dimension.
class Popup {
	hitBox: Rect;
	region: LabelRegion;
	dimension: string;
	isIntensity: boolean; // else category

	constructor(hitBox: Rect, region: LabelRegion, dimension: string, isIntensity: boolean) {
		this.region = region;
		this.hitBox = hitBox;
		this.dimension = dimension;
		this.isIntensity = isIntensity;
	}
}

export enum EditState {
	StartEdit,
	EndEdit,
}

export enum Modification {
	Modify,
	Delete,
}

export type EditStateChangeCallback = (newState: EditState) => void;
export type ModifyCallback = (action: Modification, region: LabelRegion) => void;
export type ChangeLabelCallback = (cursorX: number, cursorY: number, region: LabelRegion, dimension: string, isIntensity: boolean) => void;

// Draw facilitates drawing polygon labels
// World space is the pixels of the original image.
// Canvas space is the canvas pixels
export class Draw {
	public dim: Dimension | null = null;
	public activeLabelCategory: string = ''; // eg 'gravel', 'tar', 'stop sign', or 1..5 for whole image "condition" classifiers etc.
	public activeLabelIntensity: number = 0; // eg 1..5, for the extra "intensity" dimension of defects such as crocodile cracks
	public canvas: HTMLCanvasElement = document.createElement('canvas');
	public img: HTMLImageElement = document.createElement('img');
	public usableFramePortion: number = 1 / 3; // This is for whole-image labels (eg Is this a tar or a dirt road?)
	public state: State = State.None; // Are we dragging an existing vertex, or adding a new polygon, etc.
	public isCreatingRectangle: boolean = false; // Sub-state of State.NewPolygon
	public isModifyingRectangle: boolean = false; // Sub-state of State.DragVertex
	public drawText: boolean = false;
	public labels: ImageLabelSet = new ImageLabelSet();
	public curRegion: LabelRegion | null = null;
	public curDragIdx: number = -1; // Index of the vertex currently being dragged
	public ghostPoly: Polygon | null = null; // Ghost is the automatically computed circle through the 4 points that the user has entered
	public busyUpdatingGhost: boolean = false;
	public minVxDragPx = 30;
	public minClickPx = 30;
	public vpScale: Vec2 = new Vec2(0, 0); // scale from image pixels to canvas pixels
	public vpOffset: Vec2 = new Vec2(0, 0); // offset from image origin to canvas origin
	public popups: Popup[] = [];

	public onEditStateChange: EditStateChangeCallback | null = null;
	public onModifyRegion: ModifyCallback | null = null;
	public onChangeLabel: ChangeLabelCallback | null = null;

	initialize(canvas: HTMLCanvasElement) {
		this.canvas = canvas;
		this.canvas.addEventListener('mousedown', (ev) => { this.onMouseDown(ev); });
		this.canvas.addEventListener('mouseup', (ev) => { this.onMouseUp(ev); });
		this.canvas.addEventListener('mousemove', (ev) => { this.onMouseMove(ev); });
		this.canvas.addEventListener('contextmenu', (ev) => { this.onContextMenu(ev); });
		this.canvas.addEventListener('wheel', (ev) => { this.onMouseWheel(ev); });
		//window.addEventListener('mousewheel', this.onMouseWheel);
		this.zoomAll(false);
	}

	shutdown() {
		//window.removeEventListener('mousewheel', this.onMouseWheel);
	}

	get isPolygonDim(): boolean {
		return this.dim !== null && this.dim.type === DimensionType.Polygon;
	}

	get isSemanticSegmentationDim(): boolean {
		return this.isPolygonDim && this.dim !== null && this.dim.isSemanticSegmentation;
	}

	freeze() {
		this.state = State.Frozen;
	}
	unfreeze() {
		this.state = State.None;
	}

	img2can(pt: Vec2): Vec2 {
		return pt.mul(this.vpScale).add(this.vpOffset);
		//return pt.add(this.vpOffset).mul(this.vpScale);
		//return new Vec2(this.canvas.offsetWidth * pt.x / this.img.naturalWidth, this.canvas.offsetHeight * pt.y / this.img.naturalHeight);
	}

	can2img(pt: Vec2): Vec2 {
		let invScale = new Vec2(1.0 / this.vpScale.x, 1.0 / this.vpScale.y);
		let invOffset = new Vec2(-this.vpOffset.x, -this.vpOffset.y);
		return pt.add(invOffset).mul(invScale);
		//return new Vec2(this.img.naturalWidth * pt.x / this.canvas.offsetWidth, this.img.naturalHeight * pt.y / this.canvas.offsetHeight);
	}

	emitModified(region: LabelRegion) {
		if (this.onModifyRegion !== null)
			this.onModifyRegion(Modification.Modify, region);
	}

	emitDeleted(region: LabelRegion) {
		if (this.onModifyRegion !== null)
			this.onModifyRegion(Modification.Delete, region);
	}

	emitEditStateChange(newState: EditState) {
		if (this.onEditStateChange)
			this.onEditStateChange(newState);
	}

	zoomAll(paintNow: boolean = true) {
		let imgW = this.img.naturalWidth;
		let imgH = this.img.naturalHeight;
		let canW = this.canvas.clientWidth;
		let canH = this.canvas.clientHeight;
		if (imgW === undefined || imgW === 0 ||
			canW === undefined || canW === 0 ||
			canH === undefined || canH === 0)
			return;
		this.vpScale.x = canW / imgW;
		this.vpScale.y = canH / imgH;
		this.vpOffset = new Vec2(0, 0);
		//console.log('offset:', this.vpOffset.x, this.vpOffset.y, 'scale:', this.vpScale.x, this.vpScale.y);
		if (paintNow)
			this.paint();
	}

	paint() {
		if (this.vpScale.x === 0)
			this.zoomAll(false);

		let canvas = this.canvas;
		canvas.width = canvas.offsetWidth;
		canvas.height = canvas.offsetHeight;
		let cx = canvas.getContext('2d')!;

		// Normally I hate screwing with the aspect ratio of an image, but in this case, I'm actually not so sure that it's a bad thing
		let p1 = this.img2can(new Vec2(0, 0));
		let p2 = this.img2can(new Vec2(this.img.naturalWidth, this.img.naturalHeight));
		cx.drawImage(this.img, p1.x, p1.y, p2.x - p1.x, p2.y - p1.y);

		this.drawGhost(cx);

		this.paintPolygons(cx);

		if (this.dim && this.dim.type === DimensionType.WholeImage) {
			let pad = 5;
			cx.strokeStyle = 'rgba(55,150,250, 0.7)';
			cx.lineWidth = 5;
			let lwH = cx.lineWidth / 2;
			let usableY = canvas.height * (1 - this.usableFramePortion);
			cx.strokeRect(pad, usableY, canvas.width - pad * 2 + lwH, canvas.height - pad - usableY + lwH);
		}
	}

	paintPolygons(cx: CanvasRenderingContext2D) {
		if (this.dim === null)
			return;
		cx.fillStyle = '';
		this.popups = [];
		for (let region of this.labels.regionsWithPolygons()) {
			// draw region dimly if it's labelled for a different dimension to the one we're currently defining
			let isDefined = region.labels[this.dim.id] !== undefined;
			let p = region.polygon!;
			cx.beginPath();
			let pt0 = this.img2can(p.vx[0]);
			let minP = new Vec2(pt0.x, pt0.y);
			let maxP = new Vec2(pt0.x, pt0.y);
			for (let ppt of p.vx) {
				let pt = this.img2can(ppt);
				cx.lineTo(pt.x, pt.y);
				minP.x = Math.min(minP.x, pt.x);
				minP.y = Math.min(minP.y, pt.y);
				maxP.x = Math.max(maxP.x, pt.x);
				maxP.y = Math.max(maxP.y, pt.y);
			}
			cx.lineTo(pt0.x, pt0.y);
			cx.lineWidth = isDefined ? 1.5 : 1.5;
			if (region === this.curRegion) {
				cx.strokeStyle = 'rgba(210,0,200,0.9)';
				cx.stroke();
			} else {
				cx.strokeStyle = isDefined ? 'rgba(250,0,0,1)' : 'rgba(250,0,0,0.5)';
				cx.fillStyle = isDefined ? 'rgba(250,0,0,0.1)' : 'rgba(250,0,0,0.05)';
				cx.fill();
				cx.stroke();
			}
			for (let ppt of p.vx) {
				let pt = this.img2can(ppt);
				cx.fillStyle = 'rgba(0,0,0,0.7)';
				cx.beginPath();
				cx.ellipse(pt.x, pt.y, 2.5, 2.5, 0, 0, 2 * Math.PI);
				cx.fill();
			}
			if (this.drawText && this.dim !== null && region.labels[this.dim.id] !== undefined) {
				let lab = region.labels[this.dim.id];
				let category = this.dim.label2Value(lab.category);
				let mx = (minP.x + maxP.x) / 2;
				let my = (minP.y + maxP.y) / 2;
				let title = category != null ? category.title : 'UNRECOGNIZED';
				let categoryBox = this.drawTextWithHalo(cx, title, mx, my);
				this.popups.push(new Popup(categoryBox, region, this.dim.id, false));
				if (category && category.hasIntensity) {
					let intensity = lab.intensity !== undefined ? lab.intensity : 0;
					let intensityBox = this.drawTextWithHalo(cx, intensity.toFixed(0), categoryBox.x2 + 10, my);
					this.popups.push(new Popup(intensityBox, region, this.dim.id, true));
				}
			}
		}
	}

	drawGhost(cx: CanvasRenderingContext2D) {
		if (this.ghostPoly === null)
			return;
		cx.beginPath();
		for (let p of this.ghostPoly.vx) {
			let pt = this.img2can(p);
			cx.lineTo(pt.x, pt.y);
		}
		cx.closePath();
		cx.lineWidth = 3.0;
		cx.strokeStyle = 'rgba(0,180,0,1.0)';
		cx.setLineDash([2, 2]);
		cx.stroke();
	}

	// Returns the canvas coordinates of the rendered text box
	drawTextWithHalo(cx: CanvasRenderingContext2D, txt: string, x: number, y: number): Rect {
		cx.fillStyle = 'rgba(255,255,255,0.3)';
		cx.font = '16px sans-serif';
		cx.textAlign = 'center';
		cx.textBaseline = 'middle';
		let radius = 2;
		for (let dx = -radius; dx <= radius; dx++) {
			for (let dy = -radius; dy <= radius; dy++) {
				cx.fillText(txt, x + dx, y + dy);
			}
		}
		cx.fillStyle = 'rgba(0,0,0,1)';
		cx.fillText(txt, x, y);
		let metrics = cx.measureText(txt);
		let r = new Rect(-metrics.actualBoundingBoxLeft, -metrics.actualBoundingBoxAscent, metrics.actualBoundingBoxRight, metrics.actualBoundingBoxDescent);
		r.translate(x, y);
		return r;
	}

	onMouseWheel(evGen: Event) {
		if (this.state === State.Frozen)
			return;
		let ev = evGen as MouseWheelEvent;
		// this is necessary for pinch zooming to work on laptops
		ev.preventDefault();
		let zoomin = ev.deltaY < 0;
		let scale = 1.4;
		if (!zoomin)
			scale = 1.0 / scale;
		let newScale = this.vpScale.scalarMul(scale);
		let mousePt = new Vec2(ev.layerX, ev.layerY);
		let newOffset = new Vec2(0, 0);
		newOffset.x = mousePt.x - newScale.x * (mousePt.x - this.vpOffset.x) / this.vpScale.x;
		newOffset.y = mousePt.y - newScale.y * (mousePt.y - this.vpOffset.y) / this.vpScale.y;
		this.vpScale = newScale;
		this.vpOffset = newOffset;
		this.paint();
	}

	onMouseDown(ev: MouseEvent) {
		if (this.state === State.Frozen)
			return;
		if (!this.isPolygonDim)
			return;

		// forceNewRegion forces the creation of a new polygon (instead of moving an existing vertex)
		let forceNewRegion = ev.shiftKey;
		let deleteVertex = ev.ctrlKey;
		let forceDelete = ev.altKey;

		let clickPtCanvas = new Vec2(ev.layerX, ev.layerY);
		let clickPtWorld = this.can2img(new Vec2(ev.layerX, ev.layerY));

		if (this.state === State.None) {
			// decide if we're going to drag a vertex or create a new polygon
			if (!forceNewRegion) {
				let htVx = this.closestPolygonVertex(clickPtCanvas);
				let htObj = this.closestPolygon(clickPtCanvas);
				let popup = this.closestPopup(clickPtCanvas);
				if (forceDelete) {
					if (htObj !== null && htObj.distance < this.minClickPx) {
						let idx = this.labels.regions.findIndex((r: LabelRegion) => r === htObj!.region);
						this.labels.regions.splice(idx, 1);
						this.paint();
						// Make the polygon null, so that the rest of the event chain knows that this region has been deleted.
						// Critically, the region still has it's ID, so that's how we know which region to tell the server to delete.
						htObj.region.polygon = null;
						this.emitDeleted(htObj.region);
					}
					return;
				} else if (popup) {
					if (this.onChangeLabel) {
						// add a hackish 5px offset so that the mouseup is not seen by the popup
						this.onChangeLabel(ev.layerX - 12, ev.layerY + 5, popup.region, popup.dimension, popup.isIntensity);
						return;
					}
				}

				if (htVx !== null && htVx.distance < this.minClickPx) {
					if (deleteVertex && htVx.region.polygon!.vx.length > 3) {
						htVx.region.polygon!.vx.splice(htVx.idx, 1);
						this.emitModified(htVx.region);
					} else {
						// Drag an existing vertex
						this.state = State.DragVertex;
						this.curRegion = htVx.region;
						this.curDragIdx = htVx.idx;
						this.isModifyingRectangle = this.curRegion.polygon!.isRectangle;
						this.dragVertex(clickPtWorld);
						this.emitEditStateChange(EditState.StartEdit);
					}
					this.paint();
					return;
				} else if (htObj !== null && htObj.distance < this.minClickPx && !htObj.region.polygon!.isRectangle) {
					// Insert a new vertex on an edge
					this.state = State.DragVertex;
					this.curRegion = htObj.region;
					this.curRegion.polygon!.vx.splice(htObj.idx + 1, 0, clickPtWorld);
					this.curDragIdx = htObj.idx + 1;
					this.paint();
					this.emitEditStateChange(EditState.StartEdit);
				}
			}
			if (this.state === State.None && this.dim !== null && this.activeLabelCategory !== '') {
				this.state = State.NewPolygon;
				this.isCreatingRectangle = this.isSemanticSegmentationDim && !this.labels.anyLabelsForDimension(this.dim.id);
				this.curRegion = new LabelRegion();
				this.curRegion.polygon = new Polygon();
				this.curRegion.polygon.vx.push(clickPtWorld.clone());
				this.curRegion.polygon.vx.push(clickPtWorld.clone());
				if (this.isCreatingRectangle) {
					this.curRegion.polygon.vx.push(clickPtWorld.clone());
					this.curRegion.polygon.vx.push(clickPtWorld.clone());
				}
				this.labels.regions.push(this.curRegion);
				this.emitEditStateChange(EditState.StartEdit);
			}
		} else if (this.state === State.NewPolygon) {
			if (ev.button === 2 || (ev.button === 0 && ev.ctrlKey) || (ev.button === 0 && this.isCreatingRectangle)) {
				ev.preventDefault();
				if (ev.ctrlKey && this.ghostPoly !== null) {
					this.curRegion!.polygon = this.ghostPoly;
				} else if (!this.isCreatingRectangle) {
					this.curRegion!.polygon!.vx.pop();
				}
				let poly = this.curRegion!.polygon!;
				if (poly.vx.length < 3) {
					// delete if not at least 3 vertices
					this.labels.regions.pop();
				} else {
					this.emitModified(this.curRegion!);
				}
				this.state = State.None;
				this.curRegion = null;
				this.ghostPoly = null;
				this.paint();
				this.emitEditStateChange(EditState.EndEdit);
			} else {
				this.curRegion!.polygon!.vx.push(clickPtWorld);
			}
		}
	}

	onContextMenu(ev: MouseEvent) {
		if (this.state === State.Frozen)
			return;
		if (!this.isPolygonDim)
			return;

		ev.preventDefault();
	}

	onMouseUp(ev: MouseEvent) {
		if (this.state === State.Frozen)
			return;
		if (!this.isPolygonDim)
			return;

		if (this.state === State.DragVertex) {
			this.emitModified(this.curRegion!);
			this.state = State.None;
			this.curRegion = null;
			this.paint();
			this.emitEditStateChange(EditState.EndEdit);
		}
	}

	onMouseMove(ev: MouseEvent) {
		if (this.state === State.Frozen)
			return;
		let ptWorld = this.can2img(new Vec2(ev.offsetX, ev.offsetY));

		if (this.state === State.None)
			return;

		if (this.state === State.NewPolygon) {
			let p = this.curRegion!.polygon!;
			if (this.isCreatingRectangle) {
				p.vx[1].x = ptWorld.x;
				p.vx[2] = ptWorld.clone();
				p.vx[3].y = ptWorld.y;
			} else {
				p.vx[p.vx.length - 1] = ptWorld.clone();
			}
			if (p.vx.length === 4 && !this.isCreatingRectangle && !this.isSemanticSegmentationDim)
				this.updateGhost();
			else
				this.ghostPoly = null;
		} else if (this.state === State.DragVertex) {
			this.dragVertex(ptWorld);
		}
		this.paint();
	}

	private dragVertex(ptWorld: Vec2) {
		if (this.isModifyingRectangle) {
			let vx = this.curRegion!.polygon!.vx;
			vx[this.curDragIdx] = ptWorld;
			let i = this.curDragIdx;
			let j = (this.curDragIdx + 2) % 4; // j = the opposite vertex
			let x1 = Math.min(vx[i].x, vx[j].x);
			let y1 = Math.min(vx[i].y, vx[j].y);
			let x2 = Math.max(vx[i].x, vx[j].x);
			let y2 = Math.max(vx[i].y, vx[j].y);
			this.curRegion!.polygon!.setRectangle(x1, y1, x2, y2);
		} else {
			this.curRegion!.polygon!.vx[this.curDragIdx] = ptWorld;
		}
	}

	private updateGhost() {
		if (this.busyUpdatingGhost)
			return;
		this.busyUpdatingGhost = true;

		let pts = '';
		for (let p of this.curRegion!.polygon!.vx) {
			pts += p.x + ' ' + p.y + ',';
		}
		pts = pts.substr(0, pts.length - 1);
		let $this = this;

		fetch('/api/solve?circle_points=' + encodeURIComponent(pts), { method: 'GET' }).then((response) => {
			$this.busyUpdatingGhost = false;
			if (!response.ok) {
				console.log('solve: ' + response.status + ' ' + response.statusText);
			} else {
				response.json().then((jvals) => {
					$this.ghostPoly = new Polygon();
					for (let p of jvals) {
						$this.ghostPoly.vx.push(new Vec2(p[0], p[1]));
					}
					this.paint();
				});
			}
		}).catch((reason) => {
			$this.busyUpdatingGhost = false;
			console.log('solve: ' + reason);
		});
	}

	// Find closest polygon by vertex
	private closestPolygonVertex(ptCanvas: Vec2): HitTest | null {
		let minDist = 1e10;
		let best: LabelRegion | null = null;
		let bestIdx: number = -1;
		for (let region of this.labels.regionsWithPolygons()) {
			let p = region.polygon!;
			for (let i = 0; i < p.vx.length; i++) {
				let p0 = this.img2can(p.vx[i]);
				let dist = p0.distance(ptCanvas);
				if (dist < minDist) {
					minDist = dist;
					best = region;
					bestIdx = i;
				}
			}
		}
		if (best === null)
			return null;
		return new HitTest(best, bestIdx, minDist);
	}

	// Find closest polygon - perpendicular distance to polygon edge
	private closestPolygon(ptCanvas: Vec2): HitTest | null {
		let minDist = 1e10;
		let best: LabelRegion | null = null;
		let bestIdx: number = -1;
		for (let region of this.labels.regionsWithPolygons()) {
			let p = region.polygon!;
			let j = p.vx.length - 1;
			for (let i = 0; i < p.vx.length; i++) {
				let p0 = this.img2can(p.vx[j]);
				let p1 = this.img2can(p.vx[i]);
				let ptOnLine = closestPtOnLine(ptCanvas, p0, p1, true);
				let dist = ptOnLine.distance(ptCanvas);
				if (dist < minDist) {
					minDist = dist;
					best = region;
					bestIdx = j;
				}
				j = i;
			}
		}
		if (best === null)
			return null;
		return new HitTest(best!, bestIdx, minDist);
	}

	private closestPopup(ptCanvas: Vec2): Popup | null {
		for (let p of this.popups) {
			if (p.hitBox.isInsideMe(ptCanvas.x, ptCanvas.y))
				return p;
		}
		return null;
	}
}

// Snap pt to the line that runs through P1..P2
// If isSeg is true, then treat P1..P2 as a line segment.
// If isSeg is false, then treat P1..P2 as an infinitely long line.
// If mu is provided, then it is filled with the location along P1..P2, where 0 is at P1, and 1 is at P2.
function closestPtOnLine(pt: Vec2, P1: Vec2, P2: Vec2, isSeg: boolean): Vec2 {
	let mu = 0; // mu is discarded, but it could be returned if necessary
	if (pt.eq(P1)) {
		mu = 0;
		return P1;
	}
	if (pt.eq(P2)) {
		mu = 1;
		return P2;
	}
	let me = Vec2.sub(P2, P1);
	let sub2 = Vec2.sub(pt, P1);
	let L = me.dot(me);
	if (Math.abs(L) < 2.2204460492503131e-16) { // DBL_EPSILON
		mu = 0;
		return P1;
	}
	let r = sub2.dot(me) / L;
	mu = r;
	if (!isSeg || (r >= 0 && r <= 1))
		return P1.add(me.scalarMul(r));
	else {
		if (r < 0)
			return P1;
		else
			return P2;
	}
}
