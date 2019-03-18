
export class Dimension {
	public id: string = '';
	public values: string[] = [];
	public explain: string = '';
	public shortcutKeyToValue: { [key: string]: string } = {}; // map from key to value
	public valueToShortcutKey: { [value: string]: string } = {}; // map from value to key

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

	buildShortcutKeys() {
		let km: { [key: string]: string } = {};
		let vk: { [key: string]: string } = {};
		for (let v of this.values) {
			let str = v + '';
			let key = str.toUpperCase();
			for (let word of str.split(' ')) {
				key = word[0].toUpperCase();
				if (km[key] === undefined)
					break;
			}
			km[key] = str;
			vk[str] = key;
		}
		this.shortcutKeyToValue = km;
		this.valueToShortcutKey = vk;
	}
}

export class DimensionSet {
	public dims: Dimension[] = [];
	//private nullDim: Dimension = new Dimension();

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
			// make sure all values are strings
			for (let i = 0; i < dim.values.length; i++)
				dim.values[i] = dim.values[i] + '';
			ds.dims.push(dim);
		}
		ds.buildShortcutKeys();
		return ds;
	}

	constructor() {
		//this.nullDim.id = 'null_dimension';
	}

	buildShortcutKeys() {
		for (let d of this.dims)
			d.buildShortcutKeys();
	}

	fromID(id: string): Dimension | null {
		for (let d of this.dims) {
			if (d.id === id)
				return d;
		}
		return null;
	}

	ids(): string[] {
		let v = [];
		for (let d of this.dims)
			v.push(d.id);
		return v;
	}
}
