export class Vec2 {
	x: number = 0;
	y: number = 0;

	constructor(x: number = 0, y: number = 0) {
		this.x = x;
		this.y = y;
	}

	static sub(a: Vec2, b: Vec2): Vec2 {
		return new Vec2(a.x - b.x, a.y - b.y);
	}

	get length(): number {
		return Math.sqrt(this.x * this.x + this.y * this.y);
	}

	clone(): Vec2 {
		return new Vec2(this.x, this.y);
	}

	distance(b: Vec2): number {
		return Vec2.sub(this, b).length;
	}
	dot(b: Vec2): number {
		return this.x * b.x + this.y * b.y;
	}
	add(b: Vec2): Vec2 {
		return new Vec2(this.x + b.x, this.y + b.y);
	}
	scalarMul(s: number): Vec2 {
		return new Vec2(this.x * s, this.y * s);
	}
	mul(b: Vec2): Vec2 {
		return new Vec2(this.x * b.x, this.y * b.y);
	}
	eq(b: Vec2): boolean {
		return this.x === b.x && this.y === b.y;
	}
}

export class Rect {
	x1: number = 0;
	y1: number = 0;
	x2: number = 0;
	y2: number = 0;

	constructor(x1: number, y1: number, x2: number, y2: number) {
		this.x1 = x1;
		this.y1 = y1;
		this.x2 = x2;
		this.y2 = y2;
	}

	isInsideMe(x: number, y: number): boolean {
		return x >= this.x1 && x <= this.x2 && y >= this.y1 && y <= this.y2;
	}

	translate(x: number, y: number) {
		this.x1 += x;
		this.x2 += x;
		this.y1 += y;
		this.y2 += y;
	}
}

export class Polygon {
	vx: Vec2[] = [];

	// format is:
	// [[[x1,y1,x2,y2,x3,y3,...]]]
	// the triple brackets are there so that we can support multiple exterior rings one day, with interior rings, for example
	// [[[outer],[hole1],[hole2]],[[outer2],[hole1]]]
	static fromJSON(s: string): Polygon {
		let p = new Polygon();
		if (!s.startsWith('[[[') || !s.endsWith(']]]'))
			throw new Error(`Polygon '${s}' string must start with [[[ and end with ]]]`);
		let contour = s.slice(3, s.length - 3);
		let coords = contour.split(',');
		if (coords.length % 2 !== 0)
			throw new Error(`Polygon '${s}' coordinate count must be an even number (eg x1,y1,x2,y2,x3,y3)`);
		if (coords.length / 2 < 3)
			throw new Error(`Polygon '${s}' must have at least 3 vertices`);
		for (let i = 0; i < coords.length / 2; i++)
			p.vx.push(new Vec2(parseFloat(coords[i * 2]), parseFloat(coords[i * 2 + 1])));
		return p;
	}

	toJSON(): string {
		let s = '[[[';
		for (let v of this.vx) {
			s += v.x.toFixed(2) + ',' + v.y.toFixed(2) + ',';
		}
		s = s.slice(0, s.length - 1);
		s += ']]]';
		return s;
	}

	get isRectangle(): boolean {
		if (this.vx.length !== 4)
			return false;
		// For every pair of adjacent vertices, there must be a difference in exactly one
		// of the two dimensions x and y. We can use XOR to check this.
		let j = this.vx.length - 1;
		for (let i = 0; i < this.vx.length; i++) {
			let xdiff = this.vx[i].x !== this.vx[j].x ? 1 : 0;
			let ydiff = this.vx[i].y !== this.vx[j].y ? 1 : 0;
			if ((xdiff ^ ydiff) !== 1)
				return false;
			j = i;
		}
		return true;
	}

	// Assuming this polygon is a rectangle, rewrite the 4 vertices so that they are
	// ordered top-left, top-right, bottom-right, bottom-left, given a top-down coordinate system.
	normalizeRectangle() {
		if (this.vx.length !== 4)
			throw new Error('Rectangle must have 4 vertices');
		let x1 = Math.min(this.vx[0].x, this.vx[2].x);
		let y1 = Math.min(this.vx[0].y, this.vx[2].y);
		let x2 = Math.max(this.vx[0].x, this.vx[2].x);
		let y2 = Math.max(this.vx[0].y, this.vx[2].y);
		this.setRectangle(x1, y1, x2, y2);
	}

	setRectangle(x1: number, y1: number, x2: number, y2: number) {
		if (this.vx.length > 4)
			this.vx = [];
		while (this.vx.length < 4)
			this.vx.push(new Vec2(0, 0));
		this.vx[0].x = x1;
		this.vx[0].y = y1;
		this.vx[1].x = x2;
		this.vx[1].y = y1;
		this.vx[2].x = x2;
		this.vx[2].y = y2;
		this.vx[3].x = x1;
		this.vx[3].y = y2;
	}
}
