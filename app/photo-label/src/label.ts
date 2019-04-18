import { Vec2, Polygon } from './geom';

export enum DimensionType {
	WholeImage,
	Polygon,
}

export class Dimension {
	public id: string = '';
	public values: string[] = [];
	public explain: string = '';
	public type: DimensionType = DimensionType.WholeImage;
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
		let inner = this.values.join(', ');
		if (inner.length > 60)
			inner = inner.substr(0, 60) + '...';
		return '[' + inner + ']';
	}

	buildShortcutKeys() {
		let km: { [key: string]: string } = {};
		let vk: { [key: string]: string } = {};
		for (let v of this.values) {
			let str = v + '';
			let key = '';
			// try the first letters of all the words
			for (let word of str.split(' ')) {
				key = word[0].toUpperCase();
				if (km[key] === undefined)
					break;
			}
			if (km[key] !== undefined) {
				// try every character in the string
				for (let ch of str) {
					key = ch.toUpperCase();
					if (km[key] === undefined)
						break;
				}
			}
			if (km[key] !== undefined) {
				// just try ALL keys on the keyboard
				let allKeys = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890';
				for (let ch of allKeys) {
					if (km[ch] === undefined) {
						key = ch;
						break;
					}
				}
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
			if (j[k].type === 'polygon')
				dim.type = DimensionType.Polygon;
			else if (j[k].type === undefined)
				dim.type = DimensionType.WholeImage;
			else
				throw new Error(`Unrecognized dimension type ${j[k].type}`);

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

// A set of labels that have been created on an image
export class ImageLabelSet {
	regions: LabelRegion[] = [];

	constructor() {
		// Create the special 'whole image' region
		this.regions.push(new LabelRegion());
		this.regions[0].regionID = 0;
	}

	/*
	Example response from /api/db/get_labels:
	{
		"regions": {
			"0": {
				"dims": {
					"gravel_base_stones": "3"
				}
			},
			"1": {
				"region": "[[12,13,56,34,89,23]]",
				"dims": {
					"traffic_sign": "stop",
					"traffic_sign_quality": "2",
				}
			}
		}
	}
	*/

	static fromJSON(j: any): ImageLabelSet {
		let ls = new ImageLabelSet();
		let jRegions = j.regions;
		if (!jRegions)
			return ls;
		ls.regions = [];
		for (let regionID in jRegions) {
			let region = new LabelRegion();
			region.regionID = parseInt(regionID, 10);
			let jDims = jRegions[regionID].dims;
			if (jDims !== undefined)
				region.labels = jDims;
			let jPoly = jRegions[regionID].region;
			if (jPoly !== undefined) {
				region.polygon = Polygon.fromJSON(jPoly as string);
				if (region.regionID === 0)
					throw new Error('Polygon may not exist on region 0');
			}
			ls.regions.push(region);
		}
		if (ls.regionByID(0) === null) {
			let whole = new LabelRegion();
			whole.regionID = 0;
			ls.regions.push(whole);
		}
		return ls;
	}

	get wholeImageRegion(): LabelRegion {
		let r = this.regionByID(0);
		if (r === null)
			throw new Error('Region 0 not found in ImageLabelSet');
		return r;
	}

	regionsWithPolygons(): LabelRegion[] {
		let list = [];
		for (let r of this.regions) {
			if (r.polygon !== null)
				list.push(r);
		}
		return list;
	}

	regionByID(id: number): LabelRegion | null {
		for (let r of this.regions) {
			if (r.regionID === id)
				return r;
		}
		return null;
	}
}

// A label region is a region of an image (or an entire image), which has
// one or more labels assigned to it.
// If polygon is null, then this label refers to the entire image
export class LabelRegion {
	// Region 0 is special, and always refers to the entire image.
	// A region of -1 means we haven't yet saved this region on the server.
	regionID: number = -1;
	polygon: Polygon | null = null; // Region 0 has a null polygon
	labels: { [key: string]: string } = {}; // keys are dimensions (eg traffic_sign, or tar_vci), and value is the label assigned (eg 'MaxSpeed60', or 3, respectively)
}

// A label that has been modified.
// We keep a queue of these, and send them to the server every few seconds.
// We do this way so that we're not sending an update every few milliseconds.
export class DirtyRegion {
	imgPath: string;
	region: LabelRegion;
	dimid: string; // eg traffic_sign, tar_vci

	constructor(imgPath: string, region: LabelRegion, dimid: string) {
		this.imgPath = imgPath;
		this.region = region;
		this.dimid = dimid;
	}
}

export class DirtyRegionQueue {
	queue: DirtyRegion[] = [];
	private isStopped: boolean = false;

	constructor() {
		let tick = () => {
			for (let d of this.queue) {
				this.postToServer(d);
			}
			this.queue = [];
			if (!this.isStopped)
				setTimeout(tick, 500);
		};
		tick();
	}
	stop() {
		this.isStopped = true;
	}

	push(d: DirtyRegion) {
		for (let i = 0; i < this.queue.length; i++) {
			if (this.queue[i].imgPath === d.imgPath && this.queue[i].dimid === d.dimid && this.queue[i].region === d.region) {
				this.queue[i] = d;
				return;
			}
		}
		this.queue.push(d);
	}

	postToServer(d: DirtyRegion) {
		let apiURL = '/api/db/set_label?image=' + encodeURIComponent(d.imgPath) +
			'&author=' + encodeURIComponent(localStorage.getItem('author') || '') +
			'&dimension=' + encodeURIComponent(d.dimid) +
			'&value=' + encodeURIComponent(d.region.labels[d.dimid]);

		// regionID is negative until we post the region to the server
		if (d.region.regionID > 0)
			apiURL += '&region_id=' + encodeURIComponent(d.region.regionID.toFixed(0));
		if (d.region.polygon !== null)
			apiURL += '&region=' + encodeURIComponent(d.region.polygon.toJSON());

		fetch(apiURL, { method: 'POST' }).then((response) => {
			if (response.ok) {
				response.text().then((txt) => {
					// This is only necessary the first time we post a region, but the consistency of
					// just assigning it every time feels right.
					d.region.regionID = parseInt(txt, 10);
				}).catch((reason) => {
					alert('failed to parse regionID from server ' + reason);
				});
			} else {
				response.text().then((txt) => {
					alert(response.status + ' ' + response.statusText + ':\n\n' + txt);
				}).catch((reason) => {
					alert(response.status + ' ' + response.statusText);
				});
			}
		}).catch((reason) => {
			alert(reason);
		});
	}
}
