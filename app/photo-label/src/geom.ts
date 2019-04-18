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
}
