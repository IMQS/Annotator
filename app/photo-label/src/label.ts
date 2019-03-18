export class Dimension {
	public id: string = '';
	public values: string[] = [];
	public explain: string = '';

	get niceName(): string {
		let s = '';
		let cap = true;
		for (let i = 0; i < this.id.length; i++) {
			if (this.id[i] === '_') {
				cap = true;
				i++;
				s += ' ';
			}
			if (cap)
				s += this.id[i].toUpperCase();
			else
				s += this.id[i];
			cap = false;
		}
		return s;
	}
	get niceValues(): string {
		return '[' + this.values.join(', ') + ']';
	}
}

export class DimensionSet {
	public dims: Dimension[] = [];
	private nullDim: Dimension = new Dimension();

	static fetch(): Promise<DimensionSet> {
		return new Promise<DimensionSet>((resolve, reject) => {
			fetch('/api/dimensions').then((r) => {
				r.json().then((jr) => {
					resolve(DimensionSet.fromJson(jr));
				});
			});
		});
	}

	static fromJson(j: any): DimensionSet {
		let ds = new DimensionSet();
		for (let k in j) {
			let dim = new Dimension();
			dim.id = k;
			dim.explain = j[k].explain;
			dim.values = j[k].values;
			ds.dims.push(dim);
		}
		return ds;
	}

	constructor() {
		this.nullDim.id = 'null_dimension';
	}

	fromID(id: string): Dimension {
		for (let d of this.dims) {
			if (d.id === id)
				return d;
		}
		return this.nullDim;
	}

	ids(): string[] {
		let v = [];
		for (let d of this.dims)
			v.push(d.id);
		return v;
	}
}
