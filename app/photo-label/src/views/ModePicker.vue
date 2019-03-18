<template>
  <div class='outer'>
	<div class='inner'>
		<div style='margin-bottom: 1em'>Choose the type of labels</div>
		<div v-for='dim in dimensions.dims' :key='dim.id' style='font-size: 1rem; display: flex; align-items: center'>
			<router-link :to='{name: "label", params: {dimid: dim.id}}' class='button'>
				<span style='font-size: 1.3rem'>{{dim.niceName}}: </span>
				<span style='font-size: 0.9rem'>{{dim.explain}}</span>
				<span style='font-size: 0.9rem; color: #281; margin-left: 1em'>{{dim.niceValues}}</span>
			</router-link>
		</div>
	</div>
  </div>
</template>

<script lang="ts">
import { Component, Vue } from 'vue-property-decorator';
import { DimensionSet } from '@/label';
// import HelloWorld from '@/components/HelloWorld.vue'; // @ is an alias to /src

@Component({
})
export default class ModePicker extends Vue {
	dimensions: DimensionSet = new DimensionSet();

	mounted() {
		DimensionSet.fetch().then((dset: DimensionSet) => {
			this.dimensions = dset;
		});
	}
}
</script>

<style lang="scss" scoped>
.outer {
	display: flex;
	justify-content: center;
}
.inner {
	font-size: 2rem;
	margin: 2em;
	display: flex;
	flex-direction: column;
}
.button {
	margin: 5px;
	font-size: 1rem;
	padding: 0.5em 0.6em;
	border-radius: 3px;
	border: solid 1px #aaa;
	box-shadow: 1px 1px 5px rgba(0, 0, 0, 0.1);
	background: #eee;
	color: #000;
	text-decoration: none;
	cursor: pointer;
}
.button:hover {
	border: solid 1px #000;
	background: #f3f3f3;
	text-decoration: none;
	cursor: pointer;
}
</style>
