import { Vec2, Polygon } from './geom';
import { ImageLabelSet, LabelRegion, Dimension, DimensionType } from './label';

enum State {
	None,
	NewPolygon,
	DragVertex,
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

export enum Modification {
	Modify,
	Delete,
}

export type ModifyCallback = (action: Modification, region: LabelRegion) => void;

// Draw facilitates drawing polygon labels
// World space is the pixels of the original image.
// Canvas space is the canvas pixels
export class Draw {
	public dim: Dimension | null = null;
	public activeLabel: string = ''; // eg 1..5, or 'gravel', 'tar', 'stop sign', etc.
	public canvas: HTMLCanvasElement = document.createElement('canvas');
	public img: HTMLImageElement = document.createElement('img');
	public usableFramePortion: number = 1 / 3;
	public state: State = State.None;
	public drawText: boolean = false;
	public labels: ImageLabelSet = new ImageLabelSet();
	public curRegion: LabelRegion | null = null;
	public curDragIdx: number = -1;
	public ghostPoly: Polygon | null = null;
	public busyUpdatingGhost: boolean = false;
	public minVxDragPx = 30;
	public minClickPx = 30;
	public vpScale: Vec2 = new Vec2(0, 0); // scale from image pixels to canvas pixels
	public vpOffset: Vec2 = new Vec2(0, 0); // offset from image origin to canvas origin

	public onModifyRegion: ModifyCallback | null = null;

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

	emitModified() {
		if (this.onModifyRegion !== null)
			this.onModifyRegion(Modification.Modify, this.curRegion!);
	}

	emitDeleted(region: LabelRegion) {
		if (this.onModifyRegion !== null)
			this.onModifyRegion(Modification.Delete, region);
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
				let val = this.dim.label2Value(region.labels[this.dim.id]);
				let mx = (minP.x + maxP.x) / 2;
				let my = (minP.y + maxP.y) / 2;
				let title = val !== null ? val.title : 'UNRECOGNIZED';
				this.drawTextWithHalo(cx, title, mx, my);
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

	drawTextWithHalo(cx: CanvasRenderingContext2D, txt: string, x: number, y: number) {
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
	}

	onMouseWheel(evGen: Event) {
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
		if (!this.isPolygonDim)
			return;

		// Hold down ctrl to force creation of a new polygon (instead of moving an existing vertex)
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
				}
				if (htVx !== null && htVx.distance < this.minClickPx) {
					if (deleteVertex && htVx.region.polygon!.vx.length > 3) {
						htVx.region.polygon!.vx.splice(htVx.idx, 1);
					} else {
						this.state = State.DragVertex;
						this.curRegion = htVx.region;
						this.curDragIdx = htVx.idx;
						this.curRegion!.polygon!.vx[this.curDragIdx] = clickPtWorld;
					}
					this.paint();
					return;
				} else if (htObj !== null && htObj.distance < this.minClickPx) {
					this.state = State.DragVertex;
					this.curRegion = htObj.region;
					this.curRegion.polygon!.vx.splice(htObj.idx + 1, 0, clickPtWorld);
					this.curDragIdx = htObj.idx + 1;
					this.paint();
				}
			}
			if (this.state === State.None && this.dim !== null && this.activeLabel !== '') {
				this.state = State.NewPolygon;
				this.curRegion = new LabelRegion();
				this.curRegion.polygon = new Polygon();
				this.curRegion.polygon.vx.push(clickPtWorld);
				this.curRegion.polygon.vx.push(clickPtWorld);
				this.labels.regions.push(this.curRegion);
			}
		} else if (this.state === State.NewPolygon) {
			if (ev.button === 2 || (ev.button === 0 && ev.ctrlKey)) {
				ev.preventDefault();
				if (ev.ctrlKey && this.ghostPoly !== null) {
					this.curRegion!.polygon = this.ghostPoly;
				} else {
					this.curRegion!.polygon!.vx.pop();
				}
				let poly = this.curRegion!.polygon!;
				if (poly.vx.length < 3) {
					// delete if not at least 3 vertices
					this.labels.regions.pop();
				} else {
					this.emitModified();
				}
				this.state = State.None;
				this.curRegion = null;
				this.ghostPoly = null;
				this.paint();
			} else {
				this.curRegion!.polygon!.vx.push(clickPtWorld);
			}
		}
	}

	onContextMenu(ev: MouseEvent) {
		if (!this.isPolygonDim)
			return;

		ev.preventDefault();
	}

	onMouseUp(ev: MouseEvent) {
		if (!this.isPolygonDim)
			return;

		if (this.state === State.DragVertex) {
			this.emitModified();
			this.state = State.None;
			this.curRegion = null;
			this.paint();
		}
	}

	onMouseMove(ev: MouseEvent) {
		let ptWorld = this.can2img(new Vec2(ev.offsetX, ev.offsetY));

		if (this.state === State.None)
			return;

		if (this.state === State.NewPolygon) {
			let p = this.curRegion!.polygon!;
			p.vx[p.vx.length - 1] = ptWorld;
			if (p.vx.length === 4)
				this.updateGhost();
			else
				this.ghostPoly = null;
		} else if (this.state === State.DragVertex) {
			this.curRegion!.polygon!.vx[this.curDragIdx] = ptWorld;
		}
		this.paint();
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
