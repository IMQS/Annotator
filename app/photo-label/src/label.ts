import { Polygon } from './geom';

export enum DimensionType {
	WholeImage,
	Polygon,
}

// A possible value that can be assigned to a labelled thing
// This is really just a label combined with some other metadata
export class DimensionValue {
	public label: string = ''; // This is the label that goes into the DB
	public icon: string = ''; // An image to help the user understand what this is
	public title: string = ''; // A title to help the user understand what this is
	public hasIntensity: boolean = false; // True if the user can also assign an intensity quantity to this category

	constructor(label: string, icon: string, title: string, hasIntensity: boolean) {
		this.label = label;
		this.icon = icon;
		this.title = title;
		this.hasIntensity = hasIntensity;
	}
}

export class Dimension {
	public id: string = '';
	public values: DimensionValue[] = [];
	public explain: string = '';
	public type: DimensionType = DimensionType.WholeImage;
	public isSemanticSegmentation: boolean = false;
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
		let inner = this.valueTitles.join(', ');
		if (inner.length > 60)
			inner = inner.substr(0, 60) + '...';
		return '[' + inner + ']';
	}

	get valueTitles(): string[] {
		let t = [];
		for (let v of this.values)
			t.push(v.title);
		return t;
	}

	label2Value(label: string): DimensionValue | null {
		for (let v of this.values) {
			if (v.label === label)
				return v;
		}
		return null;
	}

	buildShortcutKeys() {
		let km: { [key: string]: string } = {};
		let vk: { [key: string]: string } = {};
		for (let v of this.valueTitles) {
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
			if (j[k].type === 'polygon')
				dim.type = DimensionType.Polygon;
			else if (j[k].type === undefined)
				dim.type = DimensionType.WholeImage;
			else
				throw new Error(`Unrecognized dimension type ${j[k].type}`);

			dim.isSemanticSegmentation = j[k].semanticSegmentation === true;

			if (j[k].values instanceof Array) {
				// values: [1,2,3,4,5]
				for (let label of j[k].values) {
					dim.values.push(new DimensionValue(label, '', label, false));
				}
			} else {
				// values: {
				//     "R101": { "title": "Stop", "icon": "stop.jpg" }
				//     "R102": { "title": "Yield", "icon": "yield.jpg" }
				// }
				for (let label in j[k].values) {
					let val = j[k].values[label];
					dim.values.push(new DimensionValue(label, val.icon, val.title, val.hasIntensity === true));
				}
			}

			// make sure all labels are strings
			for (let v of dim.values) {
				v.label = v.label + '';
				v.title = v.title + '';
			}
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
			if (jDims !== undefined) {
				for (let dim in jDims) {
					let val = jDims[dim];
					region.labels[dim] = new LabelValue(val.category, val.intensity);
				}
			}
			let jPoly = jRegions[regionID].region;
			if (jPoly !== undefined) {
				region.polygon = Polygon.fromJSON(jPoly as string);
				if (region.regionID === 0)
					throw new Error('Polygon may not exist on region 0');
			}
			ls.regions.push(region);
		}
		if (ls.regionByID(0) === null) {
			// Add the special 'whole image' region
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

	// Returns true if there are any labels for the given dimension
	anyLabelsForDimension(dim: string): boolean {
		for (let r of this.regions) {
			for (let labKey in r.labels) {
				if (labKey === dim)
					return true;
			}
		}
		return false;
	}

	regionByID(id: number): LabelRegion | null {
		for (let r of this.regions) {
			if (r.regionID === id)
				return r;
		}
		return null;
	}
}

// A label value is the thing that the user has chosen for this region+dimension combo.
// A label value can be just the category (such as stop sign, yield sign, etc), or it
// can also include an intensity value (such as crocodile crack, intensity 3).
// For dimensions that only have a single value, you MUST use category. So even if the
// category is more of a real number than a discreet thing, if there is no associated
// intensity value, you must use category.
// The reason for this is that the server interprets the assignment of an empty category
// as a request to delete that label.
export class LabelValue {
	category: string; // Category is mandatory (eg stop sign, no left turn, etc)
	intensity?: number; // Intensity is optional (eg 1..5 for common tar road defects)

	constructor(category: string, intensity?: number) {
		this.category = category;
		this.intensity = intensity;
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
	labels: { [key: string]: LabelValue } = {}; // keys are dimensions (eg traffic_sign, or tar_vci), and value is the label assigned (eg 'MaxSpeed60', or 3, respectively)
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
		let labelVal = d.region.labels[d.dimid];
		let apiURL = '/api/db/set_label?image=' + encodeURIComponent(d.imgPath) +
			'&author=' + encodeURIComponent(localStorage.getItem('author') || '') +
			'&dimension=' + encodeURIComponent(d.dimid) +
			'&category=' + encodeURIComponent(labelVal.category);
		if (labelVal.intensity !== undefined)
			apiURL += '&intensity=' + encodeURIComponent(labelVal.intensity.toPrecision(3));

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
