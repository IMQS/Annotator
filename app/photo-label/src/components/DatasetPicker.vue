<template>
	<div>
		<div style='margin-bottom: 5px; font-size: 18px'>Dataset</div>
		<div style='margin-left: 10px'>
			<div v-for='d of allDatasets' v-bind:key='d'>
				<label style='cursor: pointer; line-height: 1.3'><input type='radio' name='dataset' v-model='dataset' :value='d' @click="$emit('change', d)" />{{d}}</label>
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';

@Component({})
export default class DatasetPicker extends Vue {
	allDatasets: string[] = [];
	dataset: string = '';

	@Watch('dataset')
	onDatasetChanged(newVal: string) {
		localStorage.setItem('dataset', this.dataset);
	}

	restorePrevious() {
		let cur = localStorage.getItem('dataset');
		let found = false;
		for (let d of this.allDatasets) {
			if (d === cur) {
				found = true;
				this.dataset = cur;
				break;
			}
		}
		if (!found && this.allDatasets.length !== 0) {
			this.dataset = this.allDatasets[0];
			this.$emit('change', this.dataset);
		}
	}

	mounted() {
		// load the subdirectories inside the global dataset
		fetch('/api/datasets').then((r) => {
			r.json().then((jr) => {
				this.allDatasets = jr as string[];
				// The word 'Everything' is hardcoded into Label.vue
				this.allDatasets.splice(0, 0, 'Everything');
				this.restorePrevious();
			});
		});
	}
}
</script>

<style lang="scss" scoped>
</style>
